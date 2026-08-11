package com.mizukos.usbip

import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.hardware.usb.UsbManager
import android.os.IBinder
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.*

sealed class UsbUiState {
    object Idle : UsbUiState()
    object Loading : UsbUiState()
    data class Success(val devices: List<UsbDeviceInfo>) : UsbUiState()
}

class UsbDeviceViewModel(private val usbManager: UsbManager) : ViewModel() {

    private val _uiState = MutableStateFlow<UsbUiState>(UsbUiState.Idle)
    val uiState: StateFlow<UsbUiState> = _uiState.asStateFlow()

    private val _deviceIp = MutableStateFlow("127.0.0.1")
    val deviceIp: StateFlow<String> = _deviceIp.asStateFlow()

    private val _availableDevices = MutableStateFlow<List<UsbDeviceInfo>>(emptyList())
    private val exportedDevices = MutableStateFlow<Map<String, UsbServerService.DeviceInfo>>(emptyMap())

    val devices: StateFlow<List<UsbDeviceInfo>> = combine(_availableDevices, exportedDevices) { available, exported ->
        available.map { device ->
            val exportedInfo = exported.values.find { it.deviceId == device.deviceId }
            when {
                exportedInfo == null -> device.copy(connectionState = ConnectionState.DISCONNECTED)
                exportedInfo.isConnected -> device.copy(connectionState = ConnectionState.CONNECTED)
                else -> device.copy(connectionState = ConnectionState.CONNECTING)
            }
        }
    }.stateIn(viewModelScope, SharingStarted.Eagerly, emptyList())

    private var usbService: UsbServerService? = null
    private var serviceJob: Job? = null
    private var isBound = false
    private val isInitialized = CompletableDeferred<Unit>()
    
    private val serviceConnection = object : ServiceConnection {
        override fun onServiceConnected(name: ComponentName?, service: IBinder?) {
            val binder = service as UsbServerService.LocalBinder
            usbService = binder.getService()
            
            // Cancel any previous collection job to avoid duplicates
            serviceJob?.let { it.cancel() }
            
            // Observe exported devices from service
            serviceJob = usbService?.deviceList?.onEach { newList ->
                exportedDevices.value = newList
                // Re-scan hardware on exported list change to ensure sync
                refreshDevices()
            }?.launchIn(viewModelScope)
        }

        override fun onServiceDisconnected(name: ComponentName?) {
            serviceJob?.let { it.cancel() }
            serviceJob = null
            usbService = null
            isBound = false
        }
    }

    fun bindService(context: Context) {
        if (isBound) return
        val applicationContext = context.applicationContext
        val serviceIntent = Intent(applicationContext, UsbServerService::class.java)
        
        try {
            if (applicationContext.bindService(serviceIntent, serviceConnection, Context.BIND_AUTO_CREATE)) {
                isBound = true
            }
        } catch (e: Exception) {
            android.util.Log.e("UsbDeviceViewModel", "Failed to bind service: ${e.message}")
        }
        
        // Load IP address
        viewModelScope.launch(Dispatchers.IO) {
            _deviceIp.value = getDeviceIpAddress()
        }
    }

    fun unbindService(context: Context) {
        if (!isBound) return
        try {
            context.applicationContext.unbindService(serviceConnection)
            isBound = false
        } catch (e: Exception) {
            // Ignore if already unbound
        }
    }

    init {
        viewModelScope.launch {
            try {
                _uiState.value = UsbUiState.Loading
                refreshDevices()
                isInitialized.complete(Unit)
            } catch (e: Exception) {
                android.util.Log.e("UsbDeviceViewModel", "Init failed: ${e.message}")
                isInitialized.complete(Unit) // Unblock regardless
            }
            
            // Keep UI state in sync with devices flow
            devices.collect { list ->
                _uiState.value = UsbUiState.Success(list)
            }
        }
    }

    suspend fun resetUiState() = withContext(Dispatchers.Main) {
        _uiState.value = UsbUiState.Success(devices.value)
    }

    suspend fun refreshDevices() = withContext(Dispatchers.IO) {
        val deviceList = usbManager.deviceList
        val infoList = deviceList.values.map { device ->
            val name = if (device.productName.isNullOrEmpty()) {
                "USB Device [${String.format("0x%04x", device.vendorId)}:${String.format("0x%04x", device.productId)}]"
            } else {
                device.productName!!
            }
            
            val currentState = _availableDevices.value.find { it.deviceId == device.deviceId }?.connectionState 
                ?: ConnectionState.DISCONNECTED

            UsbDeviceInfo(
                deviceName = name,
                deviceId = device.deviceId,
                devicePath = device.deviceName,
                connectionState = currentState
            )
        }
        _availableDevices.value = infoList
    }

    fun connectDevice(deviceId: Int) {
        viewModelScope.launch {
            try {
                isInitialized.await()
                _availableDevices.update { list ->
                    list.map { 
                        if (it.deviceId == deviceId) it.copy(connectionState = ConnectionState.CONNECTING) 
                        else it 
                    }
                }
                val device = usbManager.deviceList.values.find { it.deviceId == deviceId }
                if (device != null) {
                    usbService?.connectDeviceManually(device)
                } else {
                    resetUiState()
                }
            } catch (e: Exception) {
                resetUiState()
            }
        }
    }

    fun disconnectDevice(deviceId: Int) {
        viewModelScope.launch {
            try {
                isInitialized.await()
                usbService?.disconnectDeviceManually(deviceId)
            } catch (e: Exception) {
                resetUiState()
            }
        }
    }
}
