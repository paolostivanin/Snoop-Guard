# WebMonit (alpha)
Receive a notification every time your webcam is being used

To Do list:
* check whether a webcam is being used or not :white_check_mark:
* use all video dev and not only one :white_check_mark:
* check whether the mic is being used or not :white_check_mark:
* :on: server mode
* :soon: client mode

# Requirements
* GCC or Clang (both)
* GTK+-3.0 (client)
* glib-2.0 (both)
* alsa-lib (server)

# Limitations
* `ignore_apps` is applied only to the webcam check. I haven't found a way to determine by which application the mic is being used. Feel free to open a PR if you know how to do it :)
* only one microphone is supported. Again, feel free to open a PR if you wanna improve it :)

# LICENSE
GPL 3.0 and above. Have a look at the LICENSE file for more information.
