package com.mizukos.usbip

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.hardware.usb.UsbConstants
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbDeviceConnection
import android.hardware.usb.UsbManager
import android.os.Build
import android.os.IBinder
import android.widget.Toast
import androidx.core.app.NotificationCompat

import android.os.Binder
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.LinkedList
import java.util.Queue
import java.util.concurrent.ConcurrentHashMap

class UsbServerService : Service() {

    private val serviceScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private val nativeServerMutex = Mutex()

    private val binder = LocalBinder()

    inner class LocalBinder : Binder() {
        fun getService(): UsbServerService = this@UsbServerService
    }

    override fun onBind(intent: Intent?): IBinder = binder

    private val CHANNEL_ID = "UsbServerChannel"
    private val NOTIFICATION_ID = 1
    private lateinit var usbManager: UsbManager
    private var isNativeServerStarted = false

    private val ACTION_USB_PERMISSION_SERVICE = "com.mizukos.usbip.USB_PERMISSION_SERVICE"

    data class DeviceHandle(
        val device: UsbDevice,
        val connection: UsbDeviceConnection,
        val busId: String,
        val profile: SpecialDeviceProfile
    )

    enum class SpecialDeviceProfile {
        LOGITECH_G29,
        ETHERNET,
        GENERIC
    }

    data class DeviceInfo(
        val deviceId: Int,
        val busId: String,
        val productName: String,
        val vendorId: Int,
        val productId: Int,
        val isConnected: Boolean
    )

    private val openedDevices = ConcurrentHashMap<String, DeviceHandle>()
    private val deviceJobs = ConcurrentHashMap<String, Job>()
    private val pendingConnections = ConcurrentHashMap<String, Int>() // busId to deviceId
    
    // Explicit Session Guard: Prevents multiple bindings for the same physical device ID
    private val activeDeviceIdTracker = ConcurrentHashMap<Int, String>() // deviceId to busId
    
    // Tracks devices user explicitly wanted to export (survives G29 mode switch resets)
    private val authorizedBusIds = mutableSetOf<String>()
    
    // UI Synchronization
    private val _deviceList = MutableStateFlow<Map<String, DeviceInfo>>(emptyMap())
    val deviceList: StateFlow<Map<String, DeviceInfo>> = _deviceList.asStateFlow()

    // Permission Queuing
    private val permissionQueue: Queue<UsbDevice> = LinkedList()
    private var isRequestingPermission = false

    // Cached metadata for JNI (Backward compatibility for single device logic)
    @Volatile private var cachedBusId: String = "1-1"
    @Volatile private var cachedVid: Int = 0
    @Volatile private var cachedPid: Int = 0
    @Volatile private var cachedProductName: String = ""
    @Volatile private var cachedInterfaceCount: Int = 0

    // JNI accessors (Now querying the map)
    fun getExportedDeviceCount(): Int = openedDevices.size
    
    fun getBusIdAtIndex(index: Int): String {
        return openedDevices.keys().toList().getOrNull(index) ?: ""
    }

    fun getVidForBusId(busId: String): Int = openedDevices[busId]?.device?.vendorId ?: 0
    fun getPidForBusId(busId: String): Int = openedDevices[busId]?.device?.productId ?: 0
    fun getInterfaceCountForBusId(busId: String): Int = openedDevices[busId]?.device?.interfaceCount ?: 0
    
    fun getFdForBusId(busId: String): Int = openedDevices[busId]?.connection?.fileDescriptor ?: -1

    // Original accessors for single-device fallback
    fun getCachedBusId(): String = cachedBusId
    fun getCachedVid(): Int = cachedVid
    fun getCachedPid(): Int = cachedPid
    fun getCachedProductName(): String = cachedProductName
    fun getCachedInterfaceCount(): Int = cachedInterfaceCount

    fun connectDeviceManually(device: UsbDevice) {
        val busId = getBusId(device)
        authorizedBusIds.add(busId)
        handleIncomingDevice(device)
    }

    fun disconnectDeviceManually(deviceId: Int) {
        serviceScope.launch {
            val busId = activeDeviceIdTracker[deviceId]
            if (busId != null) {
                authorizedBusIds.remove(busId)
                performComprehensiveCleanup(busId, deviceId)
            } else {
                // Fallback for UI sync if tracker is missing
                val entry = openedDevices.entries.find { it.value.device.deviceId == deviceId }
                if (entry != null) {
                    performComprehensiveCleanup(entry.key, deviceId)
                }
            }
        }
    }

    private fun performComprehensiveCleanup(busId: String, deviceId: Int) {
        android.util.Log.i("UsbServerService", "Unified Cleanup Triggered: Bus $busId, Device $deviceId")
        
        // 0. Cancel any pending jobs or async handshakes
        deviceJobs.remove(busId)?.cancel()
        pendingConnections.remove(busId)
        
        // 1. Force native TCP teardown and invalidation (This calls shutdown() on sockets)
        invalidateDeviceFd(busId)
        
        // 2. Close active connection and release FD
        val handle = openedDevices.remove(busId)
        try {
            handle?.connection?.close()
        } catch (e: Exception) {
            android.util.Log.e("UsbServerService", "Error closing connection: ${e.message}")
        }
        
        // 3. Purge session trackers
        activeDeviceIdTracker.remove(deviceId)
        
        // 4. Reset legacy metadata cache if applicable
        if (cachedBusId == busId) {
            resetLegacyMetadata()
        }
        
        // 5. Reactive UI Sync
        updateUiState()
    }

    /**
     * Build a raw USB/IP OP_REP_DEVLIST body (ndev + devices + interfaces)
     * Queries UsbManager directly to ensure no ghost entries or stale metadata.
     */
    fun getExportedDevicesPayload(): ByteArray {
        val currentHardware = usbManager.deviceList.values
        // Real-time synchronization: filter hardware by active/authorized handles
        val exported = currentHardware.filter { dev ->
            openedDevices.values.any { it.device.deviceName == dev.deviceName }
        }

        if (exported.isEmpty()) {
            return ByteBuffer.allocate(4).apply { putInt(0) }.array()
        }

        // Calculate dynamic buffer size: 4 (ndev) + sum(312 + supported_intf_count * 4)
        val totalSize = 4 + exported.sumOf { dev ->
            val supportedCount = (0 until dev.interfaceCount)
                .mapNotNull { dev.getInterface(it) }
                .count { isInterfaceSupported(it) }
            312 + (minOf(supportedCount, 32) * 4)
        }
        val buffer = ByteBuffer.allocate(totalSize).order(ByteOrder.BIG_ENDIAN)

        buffer.putInt(exported.size)

        for (device in exported) {
            // Match the specific handle to retrieve our assigned Bus ID
            val handle = openedDevices.values.find { it.device.deviceName == device.deviceName }
            val busId = handle?.busId ?: "1-0"

            val startPos = buffer.position()

            // path (256 bytes) - Padded
            val pathStr = "/sys/devices/virtual/usbip/$busId"
            val pathBytes = pathStr.toByteArray()
            buffer.put(pathBytes, 0, minOf(pathBytes.size, 256))
            buffer.position(startPos + 256)

            // busid (32 bytes) - Padded
            val bIdBytes = busId.toByteArray()
            buffer.put(bIdBytes, 0, minOf(bIdBytes.size, 32))
            buffer.position(startPos + 256 + 32)

            buffer.putInt(1) // busnum
            buffer.putInt(device.deviceId) // devnum (transient ID)
            buffer.putInt(3) // speed (High Speed)

            // Dynamic Hardware Attributes
            buffer.putShort((device.vendorId and 0xFFFF).toShort())
            buffer.putShort((device.productId and 0xFFFF).toShort())
            buffer.putShort(0x0111.toShort()) // bcdDevice (G29 compliant)

            buffer.put(device.deviceClass.toByte())
            buffer.put(device.deviceSubclass.toByte())
            buffer.put(device.deviceProtocol.toByte())
            buffer.put(1.toByte()) // bConfigurationValue
            buffer.put(1.toByte()) // bNumConfigurations
            
            // Strictly validate supported interface count
            val supportedInterfaces = (0 until device.interfaceCount)
                .mapNotNull { device.getInterface(it) }
                .filter { isInterfaceSupported(it) }
            
            val intfCount = minOf(supportedInterfaces.size, 32)
            buffer.put(intfCount.toByte())

            // Interface Descriptors (4 bytes each)
            for (i in 0 until intfCount) {
                val intf = supportedInterfaces[i]
                buffer.put(intf.interfaceClass.toByte())
                buffer.put(intf.interfaceSubclass.toByte())
                buffer.put(intf.interfaceProtocol.toByte())
                buffer.put(0.toByte()) // padding
            }
        }

        android.util.Log.i("UsbServerService", "Generated dynamic DEVLIST payload for ${exported.size} device(s)")
        return buffer.array()
    }

    private val usbReceiver = object : android.content.BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            val device: UsbDevice? = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                intent.getParcelableExtra(UsbManager.EXTRA_DEVICE, UsbDevice::class.java)
            } else {
                @Suppress("DEPRECATION")
                intent.getParcelableExtra(UsbManager.EXTRA_DEVICE)
            }

            when (intent.action) {
                UsbManager.ACTION_USB_DEVICE_DETACHED -> {
                    device?.let { detachDevice(it) }
                }
                UsbManager.ACTION_USB_DEVICE_ATTACHED -> {
                    device?.let { 
                        val profile = getDeviceProfile(it)
                        val busId = getBusId(it)
                        android.util.Log.i("UsbServerService", "USB attached: ${it.deviceName} (Bus: $busId, Profile: $profile)")
                        
                        // ONLY auto-reconnect if it's a Logitech wheel recovering from a mode-switch
                        if (profile == SpecialDeviceProfile.LOGITECH_G29 && authorizedBusIds.contains(busId)) {
                            android.util.Log.i("UsbServerService", "G29 re-attachment detected for authorized bus $busId. Auto-recovering...")
                            handleIncomingDevice(it)
                        } else {
                            android.util.Log.i("UsbServerService", "Device attached but not authorized for auto-export. Waiting for user.")
                        }
                    }
                }
                ACTION_USB_PERMISSION_SERVICE -> {
                    val granted = intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false)
                    isRequestingPermission = false
                    if (granted && device != null) {
                        android.util.Log.i("UsbServerService", "Permission granted for ${device.deviceName}")
                        attachDevice(device)
                    } else {
                        android.util.Log.w("UsbServerService", "Permission denied for ${device?.deviceName}")
                    }
                    processNextPermissionRequest()
                }
            }
        }
    }

    private fun getDeviceProfile(device: UsbDevice): SpecialDeviceProfile {
        val vid = device.vendorId
        
        // Strict check: VID 1133 is 0x046D (Logitech)
        if (vid == 1133 || vid == 0x046D) {
            return SpecialDeviceProfile.LOGITECH_G29
        }
        
        // Ethernet / CDC Network - Class 0x02 (Communications) or 0xFF (Vendor Specific)
        if (device.deviceClass == 0x02 || device.deviceClass == 0xFF) {
            for (i in 0 until device.interfaceCount) {
                val intf = device.getInterface(i)
                if (intf.interfaceClass == 0x02 || intf.interfaceClass == 0x0A) return SpecialDeviceProfile.ETHERNET
            }
        }
        
        return SpecialDeviceProfile.GENERIC
    }

    private fun getBusId(device: UsbDevice): String {
        // Simplified Bus ID mapping: Using device ID or a persistent path if available
        // For now, we map based on discovery order or a simple increment if not provided
        val existing = openedDevices.values.find { it.device.deviceName == device.deviceName }
        if (existing != null) return existing.busId
        
        return "1-${openedDevices.size + 1}"
    }

    private fun handleIncomingDevice(device: UsbDevice) {
        val profile = getDeviceProfile(device)
        val busId = getBusId(device)
        
        // 1. Session State Guard: Prevent duplicate port bindings or ghost sessions
        if (activeDeviceIdTracker.containsKey(device.deviceId)) {
            android.util.Log.i("UsbServerService", "Session Guard: Device ${device.deviceId} is already active. Ignoring duplicate request.")
            return
        }

        android.util.Log.i("UsbServerService", "Incoming USB: ${device.deviceName} (VID=${device.vendorId}, PID=${device.productId}) -> Profile: $profile")
        
        // 1. & 2. Validate device for unsupported characteristics
        if (!isDeviceSupported(device)) {
            android.util.Log.w("UsbServerService", "Aborting connection: No supported interfaces found on device.")
            serviceScope.launch(Dispatchers.Main) {
                Toast.makeText(applicationContext, "This device consists exclusively of unsupported interfaces (Audio/Isochronous)", Toast.LENGTH_LONG).show()
            }
            return
        }

        pendingConnections[busId] = device.deviceId
        updateUiState()

        if (usbManager.hasPermission(device)) {
            val job = serviceScope.launch {
                attachDevice(device)
            }
            deviceJobs[busId] = job
        } else {
            permissionQueue.add(device)
            processNextPermissionRequest()
        }
    }

    private fun isInterfaceSupported(intf: android.hardware.usb.UsbInterface): Boolean {
        // Reject Audio Class interfaces
        if (intf.interfaceClass == UsbConstants.USB_CLASS_AUDIO) {
            return false
        }
        // Reject interfaces with any Isochronous endpoints
        for (j in 0 until intf.endpointCount) {
            val ep = intf.getEndpoint(j)
            if (ep.type == UsbConstants.USB_ENDPOINT_XFER_ISOC) {
                return false
            }
        }
        return true
    }

    private fun isDeviceSupported(device: UsbDevice): Boolean {
        // A device is supported if it has AT LEAST ONE supported interface
        for (i in 0 until device.interfaceCount) {
            val intf = device.getInterface(i)
            if (isInterfaceSupported(intf)) {
                return true
            }
        }
        android.util.Log.w("UsbServerService", "Validation Failed: Device ${device.deviceName} has no supported interfaces.")
        return false
    }

    private fun processNextPermissionRequest() {
        if (isRequestingPermission || permissionQueue.isEmpty()) return
        
        val device = permissionQueue.poll() ?: return
        isRequestingPermission = true
        requestPermissionForDevice(device)
    }

    private fun requestPermissionForDevice(device: UsbDevice) {
        val intent = Intent(ACTION_USB_PERMISSION_SERVICE).apply {
            setPackage(packageName) // Ensure intent is explicit for Android 14+
            putExtra(UsbManager.EXTRA_DEVICE, device)
        }
        
        val permissionIntent = android.app.PendingIntent.getBroadcast(
            this, 
            device.deviceId, 
            intent,
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                android.app.PendingIntent.FLAG_MUTABLE or android.app.PendingIntent.FLAG_UPDATE_CURRENT
            } else {
                android.app.PendingIntent.FLAG_UPDATE_CURRENT
            }
        )
        usbManager.requestPermission(device, permissionIntent)
    }

    private fun attachDevice(device: UsbDevice) {
        val profile = getDeviceProfile(device)
        val busId = getBusId(device)
        
        try {
            val connection = usbManager.openDevice(device)
            if (connection != null) {
                android.util.Log.i("UsbServerService", "Attaching device: ${device.productName} as $busId (Profile: $profile)")
                
                // Track active session before claiming interfaces
                activeDeviceIdTracker[device.deviceId] = busId
                
                // Special handling: Evict drivers except for Ethernet
                if (profile != SpecialDeviceProfile.ETHERNET) {
                    for (i in 0 until device.interfaceCount) {
                        // Strict null-safety and bounds-checking for composite interfaces
                        val intf = device.getInterface(i) ?: continue
                        
                        // Skip unsupported interfaces (Audio/ISOC) in composite devices
                        if (!isInterfaceSupported(intf)) {
                            android.util.Log.i("UsbServerService", "Skipping unsupported interface $i (Class: ${intf.interfaceClass})")
                            continue
                        }

                        try {
                            // Force claim detaches native kernel ownership if a driver is active
                            val success = connection.claimInterface(intf, true)
                            if (success) {
                                android.util.Log.i("UsbServerService", "Successfully claimed interface $i (Class: ${intf.interfaceClass})")
                            } else {
                                android.util.Log.w("UsbServerService", "Failed to claim interface $i - locked by OS or driver.")
                            }
                        } catch (e: Exception) {
                            android.util.Log.e("UsbServerService", "Exception claiming interface $i: ${e.message}")
                        }
                    }
                } else {
                    android.util.Log.i("UsbServerService", "Bypassing driver eviction for Ethernet profile")
                }

                val handle = DeviceHandle(device, connection, busId, profile)
                openedDevices[busId] = handle
                
                // Update single-device legacy cache
                updateLegacyMetadata(device, busId)
                
                // Notify native layer
                updateDeviceFd(busId, connection.fileDescriptor)
            } else {
                val errorMsg = "Failed to open connection for ${device.deviceName} (OS-level lock or denied)"
                android.util.Log.e("UsbServerService", errorMsg)
                ErrorLogger.log(errorMsg)
                serviceScope.launch(Dispatchers.Main) {
                    Toast.makeText(applicationContext, "Failed to open device connection (Locked by OS)", Toast.LENGTH_SHORT).show()
                }
            }
        } catch (e: Exception) {
            val errorMsg = "Crash-safe catch during device attach: ${e.message}"
            android.util.Log.e("UsbServerService", errorMsg)
            ErrorLogger.log(errorMsg, e)
            e.printStackTrace()
        } finally {
            pendingConnections.remove(busId)
            updateUiState()
        }
    }

    private fun updateUiState() {
        val uiMap = mutableMapOf<String, DeviceInfo>()
        
        // 1. Add currently opened/exported devices
        openedDevices.forEach { (busId, handle) ->
            uiMap[busId] = DeviceInfo(
                deviceId = handle.device.deviceId,
                busId = busId,
                productName = handle.device.productName ?: "Unknown Device",
                vendorId = handle.device.vendorId,
                productId = handle.device.productId,
                isConnected = true
            )
        }
        
        // 2. Add devices currently in the process of connecting
        pendingConnections.forEach { (busId, deviceId) ->
            if (!uiMap.containsKey(busId)) {
                val device = usbManager.deviceList.values.find { it.deviceId == deviceId }
                if (device != null) {
                    uiMap[busId] = DeviceInfo(
                        deviceId = deviceId,
                        busId = busId,
                        productName = device.productName ?: "Connecting...",
                        vendorId = device.vendorId,
                        productId = device.productId,
                        isConnected = false
                    )
                }
            }
        }
        
        _deviceList.value = uiMap
    }

    private fun detachDevice(device: UsbDevice) {
        // Remove from permission queue if present
        permissionQueue.removeAll { it.deviceName == device.deviceName }
        
        // Find by device name (path) or ID to ensure unified cleanup
        val busId = activeDeviceIdTracker[device.deviceId] ?: 
                    openedDevices.entries.find { it.value.device.deviceName == device.deviceName }?.key ?:
                    getBusId(device)
        
        performComprehensiveCleanup(busId, device.deviceId)
    }

    private fun resetLegacyMetadata() {
        cachedVid = 0
        cachedPid = 0
        cachedProductName = ""
        cachedInterfaceCount = 0
        cachedBusId = "1-0"
    }

    private fun updateLegacyMetadata(device: UsbDevice, busId: String) {
        cachedVid = device.vendorId
        cachedPid = device.productId
        cachedProductName = device.productName ?: "Unknown Device"
        cachedInterfaceCount = device.interfaceCount
        cachedBusId = busId
    }

    override fun onCreate() {
        super.onCreate()
        android.util.Log.i("UsbServerService", "Service onCreate")
        usbManager = getSystemService(USB_SERVICE) as UsbManager
        createNotificationChannel()

        val notification = createNotification()
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            startForeground(NOTIFICATION_ID, notification, ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE)
        } else {
            startForeground(NOTIFICATION_ID, notification)
        }

        val filter = android.content.IntentFilter().apply {
            addAction(UsbManager.ACTION_USB_DEVICE_ATTACHED)
            addAction(UsbManager.ACTION_USB_DEVICE_DETACHED)
            addAction(ACTION_USB_PERMISSION_SERVICE)
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            registerReceiver(usbReceiver, filter, RECEIVER_NOT_EXPORTED)
        } else {
            registerReceiver(usbReceiver, filter)
        }
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        android.util.Log.i("UsbServerService", "Service onStartCommand")
        
        serviceScope.launch {
            nativeServerMutex.withLock {
                if (!isNativeServerStarted) {
                    android.util.Log.i("UsbServerService", "Starting persistent native server daemon")
                    startNativeServer(-1) // Start as daemon without initial device
                    isNativeServerStarted = true
                }
            }

            intent?.let {
                val device: UsbDevice? = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                    it.getParcelableExtra("USB_DEVICE", UsbDevice::class.java)
                } else {
                    @Suppress("DEPRECATION")
                    it.getParcelableExtra("USB_DEVICE")
                }
                
                device?.let { usbDevice ->
                    // User explicitly triggered this via Intent (usually from Connect button)
                    val busId = getBusId(usbDevice)
                    authorizedBusIds.add(busId)
                    handleIncomingDevice(usbDevice)
                }
            }
        }

        return START_STICKY // Stay running
    }

    override fun onDestroy() {
        android.util.Log.i("UsbServerService", "Service onDestroy")
        unregisterReceiver(usbReceiver)
        stopNativeServer()
        openedDevices.values.forEach { it.connection.close() }
        openedDevices.clear()
        _deviceList.value = emptyMap()
        serviceScope.cancel()
        super.onDestroy()
    }

    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val serviceChannel = NotificationChannel(
                CHANNEL_ID,
                "USB/IP Server Service Channel",
                NotificationManager.IMPORTANCE_DEFAULT
            )
            val manager = getSystemService(NotificationManager::class.java)
            manager.createNotificationChannel(serviceChannel)
        }
    }

    private fun createNotification(): Notification {
        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("USB/IP Server")
            .setContentText("Server is running in the background")
            .setSmallIcon(android.R.drawable.ic_menu_share) // Using a system icon for now
            .build()
    }

    /**
     * A native method that is implemented by the 'usbip_server' native library.
     */
    private external fun startNativeServer(deviceFd: Int)
    private external fun stopNativeServer()
    private external fun updateDeviceFd(busId: String, newFd: Int)
    private external fun invalidateDeviceFd(busId: String)

    companion object {
        init {
            System.loadLibrary("usbip_server")
        }
    }
}
