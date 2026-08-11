package com.mizukos.usbip

import android.content.Intent
import android.hardware.usb.UsbManager
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Info
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
import androidx.lifecycle.lifecycleScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch

class MainActivity : ComponentActivity() {

    private lateinit var usbManager: UsbManager
    private lateinit var viewModel: UsbDeviceViewModel

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        // Global error catching
        val defaultHandler = Thread.getDefaultUncaughtExceptionHandler()
        Thread.setDefaultUncaughtExceptionHandler { thread, throwable ->
            ErrorLogger.log("FATAL EXCEPTION: ${throwable.message}", throwable)
            defaultHandler?.uncaughtException(thread, throwable)
        }

        // Initialize USB manager without blocking main thread
        usbManager = getSystemService(USB_SERVICE) as UsbManager
        
        viewModel = androidx.lifecycle.ViewModelProvider(this, object : androidx.lifecycle.ViewModelProvider.Factory {
            override fun <T : androidx.lifecycle.ViewModel> create(modelClass: Class<T>): T {
                @Suppress("UNCHECKED_CAST")
                return UsbDeviceViewModel(usbManager) as T
            }
        })[UsbDeviceViewModel::class.java]

        lifecycleScope.launch(Dispatchers.IO) {
            // Start the service as a persistent daemon using application context to avoid leaks
            val applicationContext = applicationContext
            val serviceIntent = Intent(applicationContext, UsbServerService::class.java)
            ContextCompat.startForegroundService(applicationContext, serviceIntent)
            
            // Bind to service for interaction using ViewModel which now uses application context
            viewModel.bindService(applicationContext)
            
            // Process any cold-start intent safely
            intent?.let { processIntent(it) }
        }

        setContent {
            MaterialTheme(colorScheme = darkColorScheme()) {
                Surface(
                    modifier = Modifier.fillMaxSize(),
                    color = MaterialTheme.colorScheme.background
                ) {
                    val uiState by viewModel.uiState.collectAsStateWithLifecycle()
                    MainScreen(uiState, viewModel)
                }
            }
        }
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        lifecycleScope.launch {
            processIntent(intent)
        }
    }

    private fun processIntent(intent: Intent) {
        // Safe processing of USB intents off-main-thread if needed via ViewModel
        // Downstream logic in ViewModel awaits initialization
        if (intent.action == UsbManager.ACTION_USB_DEVICE_ATTACHED || 
            intent.action == UsbManager.ACTION_USB_DEVICE_DETACHED) {
            lifecycleScope.launch {
                viewModel.refreshDevices()
                viewModel.resetUiState()
            }
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        viewModel.unbindService(this)
    }

    @OptIn(ExperimentalMaterial3Api::class)
    @Composable
    fun MainScreen(uiState: UsbUiState, viewModel: UsbDeviceViewModel) {
        val deviceIp by viewModel.deviceIp.collectAsStateWithLifecycle()
        val context = androidx.compose.ui.platform.LocalContext.current

        Scaffold(
            topBar = {
                TopAppBar(
                    title = { Text("USB/IP Server Hub") },
                    actions = {
                        IconButton(onClick = { 
                            lifecycleScope.launch {
                                viewModel.refreshDevices()
                            }
                        }) {
                            Icon(Icons.Default.Refresh, contentDescription = "Refresh")
                        }
                    }
                )
            },
            floatingActionButton = {
                FloatingActionButton(
                    onClick = { ErrorLogger.copyLogsToClipboard(context) },
                    containerColor = MaterialTheme.colorScheme.tertiaryContainer,
                    contentColor = MaterialTheme.colorScheme.onTertiaryContainer
                ) {
                    Icon(Icons.Default.Info, contentDescription = "Copy Support Logs")
                }
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
                        
                        val anyExported = (uiState as? UsbUiState.Success)?.devices?.any { 
                            it.connectionState == ConnectionState.CONNECTED 
                        } ?: false
                        
                        Text(
                            text = if (anyExported) "Status: Connected active device(s)" 
                                   else "Status: Persistent Daemon Running",
                            style = MaterialTheme.typography.bodySmall,
                            fontWeight = FontWeight.Bold,
                            color = if (anyExported) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSecondaryContainer
                        )
                    }
                }

                when (uiState) {
                    is UsbUiState.Loading, UsbUiState.Idle -> {
                        Box(
                            modifier = Modifier.weight(1f).fillMaxWidth(),
                            contentAlignment = Alignment.Center
                        ) {
                            CircularProgressIndicator()
                        }
                    }
                    is UsbUiState.Success -> {
                        if (uiState.devices.isEmpty()) {
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
                                items(uiState.devices, key = { it.deviceId }) { device ->
                                    DeviceCard(device, viewModel)
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    @Composable
    fun DeviceCard(device: UsbDeviceInfo, viewModel: UsbDeviceViewModel) {
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
                        text = "CONNECTED ACTIVE",
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
                            Text(text = if (isExported) "Disconnect Device" else "Connect Device")
                        }
                    }
                }
            }
        }
    }
}
