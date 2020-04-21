scp build/alles.bin 192.168.86.20:esp/alles/build/
ssh 192.168.86.20 "cd esp/alles; ./flash.sh"
