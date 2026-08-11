package com.mizukos.usbip

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.widget.Toast
import java.text.SimpleDateFormat
import java.util.*
import java.util.concurrent.ConcurrentLinkedQueue

object ErrorLogger {
    private val logBuffer = ConcurrentLinkedQueue<String>()
    private val maxLogs = 50

    fun log(message: String, throwable: Throwable? = null) {
        val timestamp = SimpleDateFormat("HH:mm:ss.SSS", Locale.getDefault()).format(Date())
        val logEntry = "[$timestamp] $message" + (throwable?.let { "\n${android.util.Log.getStackTraceString(it)}" } ?: "")
        
        logBuffer.add(logEntry)
        while (logBuffer.size > maxLogs) {
            logBuffer.poll()
        }
        
        android.util.Log.e("USBIP_Error", message, throwable)
    }

    fun copyLogsToClipboard(context: Context) {
        val report = StringBuilder()
        report.append("--- USB/IP Support Report ---\n")
        report.append("Device: ${android.os.Build.MODEL} (API ${android.os.Build.VERSION.SDK_INT})\n")
        report.append("Time: ${Date()}\n\n")
        
        if (logBuffer.isEmpty()) {
            report.append("No errors captured.")
        } else {
            logBuffer.forEach { report.append(it).append("\n\n") }
        }

        val clipboard = context.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
        val clip = ClipData.newPlainText("USBIP Support Logs", report.toString())
        clipboard.setPrimaryClip(clip)
        
        Toast.makeText(context, "Support logs copied to clipboard", Toast.LENGTH_SHORT).show()
    }
}
