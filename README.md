sudo memtool md 0xff200000
dmesg -w
candump can1
sudo ip link set can1 type can bitrate 500000
sudo ip link set can1 up
sudo ip link set can1 down
sudo ip link del can1

sudo ip link set {can1,can2,can3,can4,can5} up

sudo ip link set can0 type can bitrate 500000 &&
sudo ip link set can0 up &&
sudo ip link set can1 type can bitrate 500000 &&
sudo ip link set can1 up &&
cansend can0 123#DEADBEEF


sudo ip link set can1 down || make uninstall && make install && sudo ip link set can1 up && candump any -tA
