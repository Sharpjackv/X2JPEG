# X2JPEG
X2JPEG is a server/client made for self hosted remote desktop gaming on Linux.

Created and optimized for the X server on linux, This software was made to fill the gaps of my current remote desktop NoVNC. While a great remote solution it lacks in low latency and high streamed fps. So I made a very similar piece of software that removes and simplifies the internal logic of NoVNC that is used to reduce bandwith. This removes nearly all overhead for the procsess at the tradeoff of increased bandwith and lower image quality. Because of how JPEG compression works this software is quite terrible at displaying small text and noisy images, but I find it to be quite useable for games escpesially on smaller screens where large pixel counts dont matter as much.

Pros :
  Smooth low latency remote desktop capture and streaming
  Frame rates in excess of 500fps
  Can work on a single X window in a "kiosk" mode
  Mouse Capture for first person games
  Simple websocket server

Cons :
  Sends entire screen image each frame
  Increased bandwith usage
  Lower image quality at similiar bandwith than other options
  Poor at displaying lots of small text
