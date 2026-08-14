package com.mizukos.usbip

import android.content.Intent
import android.hardware.usb.UsbManager
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.widget.*
import androidx.activity.ComponentActivity
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.lifecycleScope
import androidx.lifecycle.repeatOnLifecycle
import androidx.recyclerview.widget.DiffUtil
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import com.google.android.material.card.MaterialCardView
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.launch

class MainActivity : ComponentActivity() {

    private lateinit var usbManager: UsbManager
    private lateinit var viewModel: UsbDeviceViewModel
    private lateinit var deviceAdapter: DeviceAdapter

    private lateinit var tvServerIp: TextView
    private lateinit var tvServiceStatus: TextView
    private lateinit var rvDevices: RecyclerView
    private lateinit var tvEmptyState: TextView
    private lateinit var btnRefresh: ImageButton

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        
        // Keep screen on while app is in foreground
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        
        setContentView(R.layout.activity_main)

        // Global error catching
        val defaultHandler = Thread.getDefaultUncaughtExceptionHandler()
        Thread.setDefaultUncaughtExceptionHandler { thread, throwable ->
            ErrorLogger.log("FATAL EXCEPTION: ${throwable.message}", throwable)
            defaultHandler?.uncaughtException(thread, throwable)
        }

        initViews()
        setupViewModel()
        setupRecyclerView()
        observeState()

        lifecycleScope.launch(Dispatchers.IO) {
            val appCtx = applicationContext
            val serviceIntent = Intent(appCtx, UsbServerService::class.java)
            appCtx.startForegroundService(serviceIntent)
            
            viewModel.bindService(appCtx)
            
            intent?.let { processIntent(it) }
        }
    }

    private fun initViews() {
        tvServerIp = findViewById(R.id.tv_server_ip)
        tvServiceStatus = findViewById(R.id.tv_service_status)
        rvDevices = findViewById(R.id.rv_devices)
        tvEmptyState = findViewById(R.id.tv_empty_state)
        val btnCopyLogs: Button = findViewById(R.id.btn_copy_logs)
        btnRefresh = findViewById(R.id.btn_refresh)

        btnRefresh.setOnClickListener {
            lifecycleScope.launch { viewModel.refreshDevices() }
        }

        btnCopyLogs.setOnClickListener {
            ErrorLogger.copyLogsToClipboard(this)
        }
    }

    private fun setupViewModel() {
        usbManager = getSystemService(USB_SERVICE) as UsbManager
        viewModel = ViewModelProvider(this, object : ViewModelProvider.Factory {
            override fun <T : androidx.lifecycle.ViewModel> create(modelClass: Class<T>): T {
                @Suppress("UNCHECKED_CAST")
                return UsbDeviceViewModel(usbManager) as T
            }
        })[UsbDeviceViewModel::class.java]
    }

    private fun setupRecyclerView() {
        deviceAdapter = DeviceAdapter(
            onConnect = { device -> viewModel.connectDevice(device.deviceId) },
            onDisconnect = { device -> viewModel.disconnectDevice(device.deviceId) }
        )
        rvDevices.layoutManager = LinearLayoutManager(this)
        rvDevices.adapter = deviceAdapter
    }

    private fun observeState() {
        lifecycleScope.launch {
            repeatOnLifecycle(Lifecycle.State.STARTED) {
                launch {
                    viewModel.deviceIp.collectLatest { ip ->
                        tvServerIp.text = getString(R.string.ip_label, ip)
                    }
                }
                launch {
                    viewModel.uiState.collectLatest { state ->
                        when (state) {
                            is UsbUiState.Loading -> {
                                // Could show a global progress bar if needed
                            }
                            is UsbUiState.Success -> {
                                updateDeviceList(state.devices)
                            }
                            is UsbUiState.Idle -> {}
                        }
                    }
                }
            }
        }
    }

    private fun updateDeviceList(devices: List<UsbDeviceInfo>) {
        if (devices.isEmpty()) {
            rvDevices.visibility = View.GONE
            tvEmptyState.visibility = View.VISIBLE
        } else {
            rvDevices.visibility = View.VISIBLE
            tvEmptyState.visibility = View.GONE
            deviceAdapter.submitList(devices)
        }

        val anyExported = devices.any { it.connectionState == ConnectionState.CONNECTED }
        tvServiceStatus.text = if (anyExported) getString(R.string.status_connected) else getString(R.string.status_running)
        tvServiceStatus.setTextColor(if (anyExported) getColor(R.color.colorPrimary) else getColor(R.color.colorTextSecondary))
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        lifecycleScope.launch {
            processIntent(intent)
        }
    }

    private fun processIntent(intent: Intent) {
        if (intent.action == UsbManager.ACTION_USB_DEVICE_ATTACHED) {
            val device: android.hardware.usb.UsbDevice? = if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.TIRAMISU) {
                intent.getParcelableExtra(UsbManager.EXTRA_DEVICE, android.hardware.usb.UsbDevice::class.java)
            } else {
                @Suppress("DEPRECATION")
                intent.getParcelableExtra(UsbManager.EXTRA_DEVICE)
            }
            
            if (device != null) {
                lifecycleScope.launch {
                    viewModel.refreshDevices()
                    viewModel.connectDevice(device)
                }
            }
        } else if (intent.action == UsbManager.ACTION_USB_DEVICE_DETACHED) {
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

    inner class DeviceAdapter(
        private val onConnect: (UsbDeviceInfo) -> Unit,
        private val onDisconnect: (UsbDeviceInfo) -> Unit
    ) : RecyclerView.Adapter<DeviceViewHolder>() {

        private var devices: List<UsbDeviceInfo> = emptyList()

        fun submitList(newList: List<UsbDeviceInfo>) {
            val diffCallback = object : DiffUtil.Callback() {
                override fun getOldListSize(): Int = devices.size
                override fun getNewListSize(): Int = newList.size
                override fun areItemsTheSame(oldPos: Int, newPos: Int): Boolean =
                    devices[oldPos].deviceId == newList[newPos].deviceId
                override fun areContentsTheSame(oldPos: Int, newPos: Int): Boolean =
                    devices[oldPos] == newList[newPos]
            }
            val diffResult = DiffUtil.calculateDiff(diffCallback)
            devices = newList
            diffResult.dispatchUpdatesTo(this)
        }

        override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): DeviceViewHolder {
            val view = LayoutInflater.from(parent.context).inflate(R.layout.item_device, parent, false)
            return DeviceViewHolder(view)
        }

        override fun onBindViewHolder(holder: DeviceViewHolder, position: Int) {
            val device = devices[position]
            holder.bind(device, onConnect, onDisconnect)
        }

        override fun getItemCount(): Int = devices.size
    }

    inner class DeviceViewHolder(itemView: View) : RecyclerView.ViewHolder(itemView) {
        private val card: MaterialCardView = itemView.findViewById(R.id.device_card)
        private val tvName: TextView = itemView.findViewById(R.id.tv_device_name)
        private val tvDetails: TextView = itemView.findViewById(R.id.tv_device_details)
        private val tvStatus: TextView = itemView.findViewById(R.id.tv_connection_status)
        private val progress: ProgressBar = itemView.findViewById(R.id.progress_connecting)
        private val btnAction: Button = itemView.findViewById(R.id.btn_action)

        fun bind(device: UsbDeviceInfo, onConnect: (UsbDeviceInfo) -> Unit, onDisconnect: (UsbDeviceInfo) -> Unit) {
            tvName.text = device.deviceName
            tvDetails.text = itemView.context.getString(R.string.device_details_format, device.deviceId, device.devicePath)
            
            val isExported = device.connectionState == ConnectionState.CONNECTED
            val isConnecting = device.connectionState == ConnectionState.CONNECTING

            tvStatus.visibility = if (isExported) View.VISIBLE else View.GONE
            progress.visibility = if (isConnecting) View.VISIBLE else View.GONE
            btnAction.visibility = if (isConnecting) View.GONE else View.VISIBLE

            if (isExported) {
                card.setCardBackgroundColor(getColor(R.color.green))
                btnAction.text = getString(R.string.disconnect)
                btnAction.setOnClickListener { onDisconnect(device) }
            } else {
                card.setCardBackgroundColor(getColor(R.color.colorSurface))
                btnAction.text = getString(R.string.connect)
                btnAction.setOnClickListener { onConnect(device) }
            }
        }
    }
}
