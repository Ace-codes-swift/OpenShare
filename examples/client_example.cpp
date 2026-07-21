/*
 * OpenShare
 * Copyright (C) 2026 Ace Jones / ATech
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

// Minimal example: connect to a companion and print when frames arrive.
//
//   cmake --build build-release --target openshare_client_example
//   ./build-release/openshare_client_example 192.168.1.20 9000 "XXXX-XXXX-XXXX-XXXX-XXXX"
//
#include <openshare/client.hpp>

#include <chrono>
#include <iostream>
#include <thread>

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " <host> <port> <manager-code>\n";
        return 1;
    }

    openshare::ConnectOptions opts;
    opts.host = argv[1];
    opts.port = static_cast<uint16_t>(std::stoi(argv[2]));
    opts.managerCode = argv[3];
    opts.resetCounter = true;

    openshare::ConnectError err = openshare::ConnectError::None;
    std::string message;
    auto session = openshare::connect(opts, &err, &message);
    if (!session) {
        std::cerr << "connect failed: " << message << "\n";
        return 1;
    }

    std::cout << "Connected. Remote hint "
              << session->remoteWidth() << "x" << session->remoteHeight() << "\n";

    // Keepalive-ish: nudge the pointer to the center once.
    session->sendMouseMove(0.5f, 0.5f);

    int frames = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (session->connected() && std::chrono::steady_clock::now() < deadline) {
        openshare::Frame frame;
        if (session->takeFrame(frame)) {
            ++frames;
            std::cout << "frame " << frames << ": " << frame.width << "x" << frame.height
                      << " (" << frame.bgra.size() << " bytes)\n";
        }
        openshare::CursorImage cursor;
        if (session->takeCursor(cursor)) {
            std::cout << "cursor: " << cursor.width << "x" << cursor.height
                      << " hot=(" << cursor.hotX << "," << cursor.hotY << ")\n";
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    session->disconnect();
    std::cout << "Done. Received " << frames << " frames.\n";
    return frames > 0 ? 0 : 2;
}
