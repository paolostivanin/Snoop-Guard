# Snoop Guard (beta)
<a href="https://scan.coverity.com/projects/paolostivanin-snoop-guard">
  <img alt="Coverity Scan Build Status"
       src="https://scan.coverity.com/projects/12910/badge.svg"/>
</a>

[![Run Status](https://api.shippable.com/projects/58e3d5769401b40600a7c02e/badge?branch=master)](https://app.shippable.com/github/paolostivanin/Snoop-Guard)

Receive a notification every time your webcam and/or you microphone are being used

# Todo list:
* check whether a webcam is being used or not :white_check_mark:
* use all video dev and not only one :white_check_mark:
* check whether the mic is being used or not :white_check_mark:
* server :white_check_mark:
* :on: client

# Requirements
* GCC/Clang
* GTK+-3.0 (only needed if the client is built)
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
