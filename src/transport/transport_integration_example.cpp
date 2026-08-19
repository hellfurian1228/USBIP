/*
 * transport_integration_example.cpp
 *
 * Demonstrates how to wire the dual-transport layer into the existing
 * USBIP Windows Client session loop.
 *
 * This file is NOT compiled as part of the library – it is a reference
 * integration guide showing the call sites you need to add.
 *
 * ─────────────────────────────────────────────────────────────────────────
 * STEP 1 – Configure the policy before attaching a device
 * ─────────────────────────────────────────────────────────────────────────
 *
 *   // In MainWindow::handleToggleDeviceAttach() or equivalent:
 *
 *   using namespace usbip::transport;
 *
 *   DeviceNetworkConfig cfg;
 *   cfg.policy          = TransportPolicy::HYBRID_TCP_UDP;
 *   cfg.udp_port        = 3241;          // agree with the Android host
 *   cfg.jitter_window_ms = 4;
 *
 *   // Register by devid (known after the OP_REQ_IMPORT response):
 *   TransportPolicyRegistry::instance().set(devid, cfg);
 *
 *   // OR register by VID:PID so all matching devices use the same policy:
 *   TransportPolicyRegistry::instance().set(VidPid{0x05AC, 0x12A8}, cfg);
 *
 * ─────────────────────────────────────────────────────────────────────────
 * STEP 2 – Create a HybridSession when the TCP connection is established
 * ─────────────────────────────────────────────────────────────────────────
 *
 *   auto session = std::make_unique<HybridSession>(
 *       devid, vendor_id, product_id, remote_hostname);
 *
 *   // Start the UDP receive loop (no-op if policy is TCP_ONLY)
 *   session->start_udp([](IsoFrame frame) {
 *       // Called from the UDP recv thread for each received ISO frame.
 *       // Forward frame.data[0..frame.data_length] to the virtual bus driver.
 *       // frame.is_comfort_noise == true means the packet timed out and
 *       // a zero-filled buffer was synthesised to keep microframe timing.
 *   });
 *
 * ─────────────────────────────────────────────────────────────────────────
 * STEP 3 – In the TCP receive loop, call route() for every CMD_SUBMIT
 * ─────────────────────────────────────────────────────────────────────────
 *
 *   // Assume `hdr` is a parsed usbip::header (host byte order)
 *   // and `payload` is the bytes that follow it.
 *
 *   auto tcp_send = [&tcp_socket](const void *data, std::size_t len) -> bool {
 *       return send_all(tcp_socket, data, len); // your existing TCP write helper
 *   };
 *
 *   switch (session->route(hdr, payload, tcp_send)) {
 *   case RouteResult::SentOverUdp:
 *       // ISO payload was dispatched via UDP – do NOT forward over TCP.
 *       break;
 *   case RouteResult::SendOverTcp:
 *       // Non-ISO or TCP_ONLY device – send the full packet over TCP as usual.
 *       tcp_send(&hdr, sizeof(hdr));
 *       tcp_send(payload.data(), payload.size());
 *       break;
 *   case RouteResult::Error:
 *       // UDP send failed – fall back to TCP or log and disconnect.
 *       tcp_send(&hdr, sizeof(hdr));
 *       tcp_send(payload.data(), payload.size());
 *       break;
 *   }
 *
 * ─────────────────────────────────────────────────────────────────────────
 * STEP 4 – Tear down on disconnect
 * ─────────────────────────────────────────────────────────────────────────
 *
 *   // In the disconnect handler (any thread):
 *   session->close();   // stops UDP recv thread, flushes jitter buffer,
 *                       // removes devid from policy registry
 *   session.reset();
 *
 * ─────────────────────────────────────────────────────────────────────────
 * STEP 5 – Jitter buffer polling (alternative to callback)
 * ─────────────────────────────────────────────────────────────────────────
 *
 *   // If you prefer a polling model (e.g., driven by a 1 ms timer):
 *   if (auto *jb = session->jitter_buffer()) {
 *       auto deadline = JitterBuffer::Clock::now() + jb->window();
 *       IsoFrame frame = jb->pop_ready(deadline);
 *       // forward frame to virtual bus driver
 *   }
 */

// This file intentionally contains no compilable code.
// Include the transport headers directly in your session management code:
//
//   #include "src/transport/transport_policy.h"
//   #include "src/transport/hybrid_session.h"
//   #include "src/transport/urb_demuxer.h"
