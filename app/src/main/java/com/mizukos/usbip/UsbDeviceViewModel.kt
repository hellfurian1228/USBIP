package com.mizukos.usbip

import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.hardware.usb.UsbManager
import android.os.IBinder
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.flow.*

class UsbDeviceViewModel(private val usbManager: UsbManager) : ViewModel() {

    private val _availableDevices = MutableStateFlow<List<UsbDeviceInfo>>(emptyList())
    private val exportedDevices = MutableStateFlow<Map<String, UsbServerService.DeviceInfo>>(emptyMap())

    val devices: StateFlow<List<UsbDeviceInfo>> = combine(_availableDevices, exportedDevices) { available, exported ->
        available.map { device ->
            val isExported = exported.values.any { it.deviceId == device.deviceId }
            if (isExported) {
                device.copy(connectionState = ConnectionState.CONNECTED)
            } else if (device.connectionState == ConnectionState.CONNECTING) {
                device
            } else {
                device.copy(connectionState = ConnectionState.DISCONNECTED)
            }
        }
    }.stateIn(viewModelScope, SharingStarted.Eagerly, emptyList())

    private var usbService: UsbServerService? = null
    
    private val serviceConnection = object : ServiceConnection {
        override fun onServiceConnected(name: ComponentName?, service: IBinder?) {
            val binder = service as UsbServerService.LocalBinder
            usbService = binder.getService()
            
            usbService?.deviceList?.onEach { newList ->
                exportedDevices.value = newList
            }?.launchIn(viewModelScope)
        }

        override fun onServiceDisconnected(name: ComponentName?) {
            usbService = null
        }
    }

    fun bindService(context: Context) {
        val serviceIntent = Intent(context, UsbServerService::class.java)
        context.bindService(serviceIntent, serviceConnection, Context.BIND_AUTO_CREATE)
    }

    fun unbindService(context: Context) {
        context.unbindService(serviceConnection)
    }

    init {
        refreshDevices()
    }

    fun refreshDevices() {
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
        _availableDevices.update { list ->
            list.map { 
                if (it.deviceId == deviceId) it.copy(connectionState = ConnectionState.CONNECTING) 
                else it 
            }
        }
        val device = usbManager.deviceList.values.find { it.deviceId == deviceId }
        device?.let { usbService?.connectDeviceManually(it) }
    }

    fun disconnectDevice(deviceId: Int) {
        usbService?.disconnectDeviceManually(deviceId)
    }
}
