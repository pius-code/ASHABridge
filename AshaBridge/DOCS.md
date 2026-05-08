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

I am thinking two ways of controlling

1. the agent can send commands at its own discretion individually
2. the agent can create workflows for speed... with this it sort of sends commands to the esp32 that is flashed in a certain part of memory and this will trigger it run everytime without agent itslef triggering the command

should the case of a user wanting to change the output or input or pinNumber we can just say reassign to prevent the user from reflashing the entire codes(this will happen if the user doesn't remember the codes )

format that the agent should send
for digital
{"pin": 18, "action": "digital", "value": 1}

for pwm
{"pin": 18, "action": "pwm", "channel": 0, "freq": 50, "duty": 4915}

for I2C
{"pin": 21, "action": "i2c_write", "addr": 60, "reg": 0, "data": [174]}

for SPI
{"action": "spi_write", "cs_pin": 5, "speed": 25000000, "mode": 0, "data": [1, 2, 3]}

for UART
{"action": "uart_write", "baud": 9600, "tx_pin": 17, "rx_pin": 16, "data": "AT+CREG?\r\n"}

we currently are using the free hiveMQTT broker, meaning that everyone can subscribe to our server and see the unique ID
-- what we can do is encrypt the ID on both the publisher and subscriber to ensure that even if someone gets it, its not the same

currenly esp32 has 320KB of ram , our mqtt is set to receive 16kb of message(plenty for now) but could be a limit in the future if agent sends large commands, we dont want to have a very large payload because we may need to pass that same payload into the handleCommand if its huge it could eat up the the ram and other function wouldnt run, also we have a delay in there which stops the cpu, means wwhile we have a delay command being run, if the agent sends another command it will not run

parts of the code I dont understand:
the part were we are using the instacnce of the asha to access the asha so that we can acess the handleCpmmand because it is static

Another challenge experienced is when the realtime engine(Lua) is working because it is running a heavy intensive task(like while loop or delay) in the same thread as MQTT it drops the connection, this disables wifi, and drops MQTT connection to server so we moved it to run on core 0 and let the mqtt etc run on core 1
so with the way it works the core 0 is sleeping, using the (portMAX_DELAY) until something happens(a string arrives)... its currently set to priority 1 and not 0 because the MQTT and wifi are the higher priority if the realtime engine starves the MQTT or the wifi drops then the realtime engine may not even receive its tasks in the first place

also the race condition where the the agent might be controlling the same hardware in mqtt and lua- solved by giving a unified database to the agent so that it can check whats running and stop it or warn the user.
