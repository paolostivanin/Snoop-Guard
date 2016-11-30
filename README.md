# Snoop Guard (alpha)
Receive a notification every time your webcam and/or you microphone are being used

# Todo list:
* check whether a webcam is being used or not :white_check_mark:
* use all video dev and not only one :white_check_mark:
* check whether the mic is being used or not :white_check_mark:
* :on: server mode
* :soon: client mode

# Requirements for Client and Server
* GCC/Clang (both)
* GTK+-3.0 (client only)
* glib-2.0 (both)
* alsa-lib (server only)

# Limitations
* `ignore_apps` is applied only to the webcam check. I haven't found a way to determine by which application the mic is being used. Feel free to open a PR if you know how to do it :)
* only one microphone is supported. Again, feel free to open a PR if you wanna improve it :)

# LICENSE
GPL 3.0 and above. Have a look at the LICENSE file for more information.
