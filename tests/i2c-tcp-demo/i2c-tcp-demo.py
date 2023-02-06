import json
from twisted.internet import task
from twisted.internet.defer import Deferred
from twisted.internet.protocol import ClientFactory
from twisted.protocols.basic import LineReceiver
from dataclasses import dataclass
from enum import Enum


#
# This I2C Slave device behaves as a simple memory storage
# which only holds 256 bytes addressed by one byte
# in address space from 0x00 to 0xFF
#
# To store a byte, send two bytes as one transaction
# where first byte is an address byte, second one is a data byte
#
# When more than two bytes are sent,
# next data bytes are stored in next addresses
#
# To read a byte or bytes, first send a single byte to set a start address
# then start a recv transaction
#
# Set a start address every time you read or write data
#


# i2cset -c 0x7F -r 0x20 0x11 0x22 0x33 0x44 0x55
# i2cget -c 0x7F -r 0x20 -l 0x0A

HOST = "localhost"
PORT = 16001

# do not edit
MEM_SIZE = 256


class EVENT(Enum):
    I2C_START_RECV = 0
    I2C_START_SEND = 1
    I2C_START_SEND_ASYNC = 2
    I2C_FINISH = 3
    I2C_NACK = 4


@dataclass
class I2CSlave:
    mem: bytearray = bytearray(MEM_SIZE)
    mem_addr: int = 0
    curr_addr: int = 0
    first_send: bool = True
    recv_conuter: int = 0


i2cslave = I2CSlave()


def dump_mem():
    print("Mem:")
    bytes_per_row = 32
    rows = int(MEM_SIZE / bytes_per_row)
    for i in range(0, rows):
        begin = i*bytes_per_row
        end = begin+bytes_per_row
        prefix = hex(begin)
        if i == 0:
            prefix = "0x00"
        print(prefix + ": " + i2cslave.mem[begin:end].hex(" "))

    print("\n")


def event_handler(packet):
    evt = EVENT(packet[1])
    print("Event handler: " + evt.name)

    if evt is EVENT.I2C_FINISH:
        i2cslave.recv_conuter = 0
        i2cslave.first_send = True
        dump_mem()

    return packet


def recv_handler(packet):
    print("Recv handler: byte number " + str(i2cslave.recv_conuter) +
          " from addr=" + hex(i2cslave.mem_addr) + ", val=" + hex(i2cslave.mem[i2cslave.mem_addr]))
    i2cslave.recv_conuter += 1
    resp = bytearray(packet)
    resp[1] = i2cslave.mem[i2cslave.mem_addr]
    i2cslave.mem_addr += 1
    if i2cslave.mem_addr == MEM_SIZE:
        i2cslave.mem_addr = 0
    return bytes(resp)


def send_handler(packet):
    print("Send handler: ", end='')
    if i2cslave.first_send == True:
        print("address byte: ", hex(packet[1]))
        i2cslave.mem_addr = packet[1]
        i2cslave.first_send = False
    else:
        print("data byte: ", hex(packet[1]))
        i2cslave.mem[i2cslave.mem_addr] = packet[1]
        i2cslave.mem_addr += 1
        if i2cslave.mem_addr == MEM_SIZE:
            i2cslave.mem_addr = 0
    return packet


handlers = {'E': event_handler, 'R': recv_handler, 'S': send_handler}


class PacketReceiver(LineReceiver):
    def __init__(self) -> None:
        super().__init__()

    def connectionMade(self):
        print("connected")

    def lineReceived(self, line):
        # print(line.hex(" "))
        resp = line
        if len(line) == 3:
            resp = handlers[chr(line[0])](line)

        self.sendLine(resp)


class PacketReceiverFactory(ClientFactory):
    protocol = PacketReceiver

    def __init__(self):
        self.done = Deferred()

    def clientConnectionFailed(self, connector, reason):
        print("connection failed:", reason.getErrorMessage())
        self.done.errback(reason)

    def clientConnectionLost(self, connector, reason):
        print("connection lost:", reason.getErrorMessage())
        self.done.callback(None)


def main(reactor):
    dump_mem()
    factory = PacketReceiverFactory()
    reactor.connectTCP(HOST, PORT, factory)
    return factory.done


if __name__ == "__main__":
    task.react(main)
