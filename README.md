# x11fwd
Allow WSLG to be used from Windows X11 client such as SSH.

## Usage
	PS> wsl x11fwd
	PS> $env:DISPLAY=(wsl hostname -I).trim()+":0"
	PS> ssh -Y host

## Note
No security check. Pay attention for port fowarding.
