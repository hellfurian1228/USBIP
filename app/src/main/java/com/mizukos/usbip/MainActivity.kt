package com.mizukos.usbip

import android.content.Context
import android.content.Intent
import android.hardware.usb.UsbManager
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import androidx.lifecycle.compose.collectAsStateWithLifecycle

class MainActivity : ComponentActivity() {

    private lateinit var usbManager: UsbManager
    private lateinit var viewModel: UsbDeviceViewModel

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        usbManager = getSystemService(USB_SERVICE) as UsbManager
        
        viewModel = androidx.lifecycle.ViewModelProvider(this, object : androidx.lifecycle.ViewModelProvider.Factory {
            override fun <T : androidx.lifecycle.ViewModel> create(modelClass: Class<T>): T {
                @Suppress("UNCHECKED_CAST")
                return UsbDeviceViewModel(usbManager) as T
            }
        })[UsbDeviceViewModel::class.java]

        // Start the service as a persistent daemon
        val serviceIntent = Intent(this, UsbServerService::class.java)
        ContextCompat.startForegroundService(this, serviceIntent)
        
        // Bind to service for interaction
        viewModel.bindService(this)

        setContent {
            MaterialTheme(colorScheme = darkColorScheme()) {
                Surface(
                    modifier = Modifier.fillMaxSize(),
                    color = MaterialTheme.colorScheme.background
                ) {
                    MainScreen(viewModel)
                }
            }
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        viewModel.unbindService(this)
    }

    @OptIn(ExperimentalMaterial3Api::class)
    @Composable
    fun MainScreen(viewModel: UsbDeviceViewModel) {
        val devices by viewModel.devices.collectAsStateWithLifecycle()
        val context = androidx.compose.ui.platform.LocalContext.current
        val deviceIp = remember { getDeviceIpAddress(context) }
        
        // Derive global status from devices flow
        val anyExported = devices.any { it.connectionState == ConnectionState.CONNECTED }

        Scaffold(
            topBar = {
                TopAppBar(
                    title = { Text("USB/IP Server Hub") },
                    actions = {
                        IconButton(onClick = { viewModel.refreshDevices() }) {
                            Icon(Icons.Default.Refresh, contentDescription = "Refresh")
                        }
                    }
                )
            }
        ) { padding ->
            Column(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(padding)
            ) {
                Surface(
                    color = MaterialTheme.colorScheme.secondaryContainer,
                    modifier = Modifier.fillMaxWidth()
                ) {
                    Column(modifier = Modifier.padding(16.dp)) {
                        Text(
                            text = "Server IP: $deviceIp",
                            style = MaterialTheme.typography.titleMedium,
                            color = MaterialTheme.colorScheme.onSecondaryContainer
                        )
                        Text(
                            text = "Port: 3240",
                            style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.onSecondaryContainer
                        )
                        Spacer(modifier = Modifier.height(8.dp))
                        Text(
                            text = if (anyExported) "Status: Exporting ${devices.count { it.connectionState == ConnectionState.CONNECTED }} device(s)" 
                                   else "Status: Persistent Daemon Running",
                            style = MaterialTheme.typography.bodySmall,
                            fontWeight = FontWeight.Bold,
                            color = if (anyExported) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSecondaryContainer
                        )
                    }
                }

                if (devices.isEmpty()) {
                    Box(
                        modifier = Modifier.weight(1f).fillMaxWidth(),
                        contentAlignment = Alignment.Center
                    ) {
                        Text("No USB devices detected", style = MaterialTheme.typography.bodyLarge)
                    }
                } else {
                    LazyColumn(
                        modifier = Modifier.weight(1f).fillMaxWidth(),
                        contentPadding = PaddingValues(16.dp),
                        verticalArrangement = Arrangement.spacedBy(12.dp)
                    ) {
                        items(devices, key = { it.deviceId }) { device ->
                            DeviceCard(device)
                        }
                    }
                }
            }
        }
    }

    @Composable
    fun DeviceCard(device: UsbDeviceInfo) {
        val isExported = device.connectionState == ConnectionState.CONNECTED
        val isConnecting = device.connectionState == ConnectionState.CONNECTING

        ElevatedCard(
            modifier = Modifier.fillMaxWidth(),
            colors = CardDefaults.elevatedCardColors(
                containerColor = if (isExported) Color(0xFF1B5E20) // Deep Green
                                else MaterialTheme.colorScheme.surface
            )
        ) {
            Column(
                modifier = Modifier.padding(16.dp)
            ) {
                Text(
                    text = device.deviceName,
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.Bold,
                    color = if (isExported) Color.White else Color.Unspecified
                )
                Spacer(modifier = Modifier.height(4.dp))
                Text(
                    text = "ID: ${device.deviceId} | Path: ${device.devicePath}",
                    style = MaterialTheme.typography.bodySmall,
                    color = if (isExported) Color.LightGray else MaterialTheme.colorScheme.onSurfaceVariant
                )
                
                if (isExported) {
                    Text(
                        text = "EXPORTED ACTIVE",
                        style = MaterialTheme.typography.labelSmall,
                        color = Color.Cyan,
                        fontWeight = FontWeight.Bold
                    )
                }

                Spacer(modifier = Modifier.height(12.dp))
                
                Box(modifier = Modifier.fillMaxWidth(), contentAlignment = Alignment.CenterEnd) {
                    if (isConnecting) {
                        CircularProgressIndicator(modifier = Modifier.size(24.dp))
                    } else {
                        Button(
                            onClick = {
                                if (isExported) {
                                    viewModel.disconnectDevice(device.deviceId)
                                } else {
                                    viewModel.connectDevice(device.deviceId)
                                }
                            },
                            colors = ButtonDefaults.buttonColors(
                                containerColor = if (isExported) Color.Red 
                                                 else MaterialTheme.colorScheme.primary,
                                contentColor = Color.White
                            )
                        ) {
                            Text(text = if (isExported) "Stop Export" else "Export Device")
                        }
                    }
                }
            }
        }
    }
}
