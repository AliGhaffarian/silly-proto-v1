import socket
import subprocess
import libtmux
from libtmux._internal.query_list import QueryList
from libtmux.constants import PaneDirection
import selectors
import logging
import os
import sys
import re

logging.basicConfig(level="INFO")
logger = logging.getLogger()
self_sock = None
master_sock = None
slave_socks = []

def broadcast_to_slaves(data):
    logger.info("data from master")
    for slave_sock in slave_socks:
        logger.info(f"sending to {slave_sock.getpeername()}")
        slave_sock.send(data)
def unicast_to_master(data):
    logger.info("data from slave")
    ret = master_sock.send(data)
    logger.info(f"master send: {ret=}")

SILLY_PARAM_PATH="./slave/silly_params.csv"
def write_slave_id_to_params(id = 1):
    csv = f"key,type,encoding,value\n\
storage,namespace,,\n\
my_slave_id,data,u8,{id}"
    fp = open(SILLY_PARAM_PATH, "w")
    fp.write(csv)
    fp.close()

def fetch_number_of_slaves_from_master_sdkconfig():
    number_of_slaves_pattern = "CONFIG_SLAVES_NUM=(\d+)"
    conf = open("./master/sdkconfig").read()
    match = re.search(number_of_slaves_pattern, conf)
    return int(match.group(1))


activate_espidf = f"source ~/.espressif/tools/activate_idf_v6.0.2.sh"
activate_espidf = f"{activate_espidf} && export IDF_TOOLCHAIN=clang"
activate_espidf = f"{activate_espidf} && export PATH=\"/home/work/.espressif/tools/esp-clang/esp-20.1.1_20250829/esp-clang/bin:$PATH\""

if __name__ == "__main__":
    server = libtmux.Server()
    selector = selectors.DefaultSelector()
    session = server.new_session()
    print(session.name)
    os.system(f"xfce4-terminal --command 'tmux attach -t {session.name}'")

    self_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM, 0)
    self_sock.bind(("127.0.0.1", 0))
    self_sock.listen(2)

    assigned_port = self_sock.getsockname()[1]
    logger.info(f"{assigned_port=}")

    active_window = session.active_window
    master_pane = active_window.active_pane
    active_pane = active_window.active_pane

    master_cmd = f"{activate_espidf} && cd master && idf.py qemu --qemu-extra-args '-serial tcp:127.0.0.1:{assigned_port}' | tee master.log"
    master_pane.send_keys(master_cmd)

    master_sock, master_addr = self_sock.accept()
    selector.register(master_sock, selectors.EVENT_READ, broadcast_to_slaves)

    number_of_slaves = fetch_number_of_slaves_from_master_sdkconfig()

    for i in range(number_of_slaves):
        slave_pane = active_pane.split(direction=PaneDirection.Right)

        write_slave_id_to_params(i + 1)
        slave_cmd = f"{activate_espidf} && cd slave && idf.py qemu --qemu-extra-args '-serial tcp:127.0.0.1:{assigned_port}' | tee slave_{i}.log"
        slave_pane.send_keys(slave_cmd)

        slave_sock, slave_addr = self_sock.accept()
        slave_socks.append(slave_sock)
        selector.register(slave_sock, selectors.EVENT_READ, unicast_to_master)
        active_pane = slave_pane

    logger.info("enter event loop")
    while True:
        events = selector.select()
        for key, event in events:
            data = key.fileobj.recv(1024)
            if(len(data) == 0):
               exit(0)
            key.data(data)
