so the wifi connect can take a call back function to do something, currently in the main.cpp it prints to terminal

in future we want to use one big class called ASHA and have the smaller subclasses in there, GEmini says that is better

The challenege now is how to trigger a change from the dashboard easily
you may want to just add a pin and auto let the AI control it without writing another line of code(although we have this)

There is this challenge where the IoT device cant send to secure servers like HTTPS

problem: [Why is this happening?
By default, the standard HTTPClient http; is designed for unencrypted http:// traffic.

When you give it an https:// string (like your ngrok URL), it silently switches under the hood to try and do a secure handshake. However, strict modern servers (like ngrok) often refuse the connection if the ESP32 doesn't provide a root SSL security certificate to prove it knows who it's talking to, or if you don't explicitly tell the ESP32 to skip certificate verification.
]
This raises secuirty concerns, so in future we must find a way of enforcing a TLS for extra security between our server and esp32
