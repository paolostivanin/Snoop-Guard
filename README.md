# Snoop Guard (beta)
Receive a notification every time your webcam and/or you microphone are being used

# Todo list:
* check whether a webcam is being used or not :white_check_mark:
* use all video dev and not only one :white_check_mark:
* check whether the mic is being used or not :white_check_mark:
* server :white_check_mark:
* :on: client

# Requirements
* GCC/Clang
* GTK+-3.0
* glib-2.0
* alsa-lib
* libnotify

# Installation
TODO

# How To
Put the `snoop-guard.service` file under `$HOME/.config/systemd/user/`. Then execute:
* `systemctl --user daemon-reload`
* `systemctl --user enable snoop-guard`

# Limitations
* `ignore_apps` is applied only to the webcam check. I haven't found a way to determine by which application the mic is being used. Feel free to open a PR if you know how to do it :)
* only one microphone is supported. Again, feel free to open a PR if you wanna improve it :)

# LICENSE
GPL 3.0 and above. Have a look at the LICENSE file for more information.
