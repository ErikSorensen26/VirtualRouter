import gdb
import gdb.printing

def ipv4_formatter(addr: int) -> str:
    import socket, struct
    return socket.inet_ntop(socket.AF_INET, struct.pack(">I", addr & 0xFFFFFFFF))

def ipv6_formatter(hi: int, lo: int) -> str:
    import socket, struct
    packed = struct.pack(">QQ", hi, lo)
    return socket.inet_ntop(socket.AF_INET6, packed)

def ip_formatter(hi: int, lo: int) -> str:
    if hi == 0 and (lo & 0xFFFFFFFF00000000) == 0x0000FFFF00000000:
        return ipv4_formatter(lo)
    return ipv6_formatter(hi, lo)

class IPAddressPrinter:
    def __init__(self, val):
        self.val = val

    def to_string(self) -> str:
        lo = int(self.val['raw'].cast(gdb.lookup_type('unsigned long long')))
        hi = int((self.val['raw'] >> 64).cast(gdb.lookup_type('unsigned long long')))
        return ip_formatter(hi, lo)

class IPv4AddressPrinter:
    def __init__(self, val):
        self.val = val

    def to_string(self) -> str:
        raw = int(self.val['addr'])
        return ipv4_formatter(raw)

class IPv6AddressPrinter:
    def __init__(self, val):
        self.val = val

    def to_string(self) -> str:
        lo = int(self.val['addr'].cast(gdb.lookup_type('unsigned long long')))
        hi = int((self.val['addr'] >> 64).cast(gdb.lookup_type('unsigned long long')))
        return ipv6_formatter(hi, lo)

class IPPrefixPrinter:
    def __init__(self, val):
        self.val = val

    def to_string(self) -> str:
        lo = int(self.val['addr'].cast(gdb.lookup_type('unsigned long long')))
        hi = int((self.val['addr'] >> 64).cast(gdb.lookup_type('unsigned long long')))
        len = int(self.val['prefixLength'])
        return f"{ip_formatter(hi, lo)}/{len}"

class IPv4PrefixPrinter:
    def __init__(self, val):
        self.val = val

    def to_string(self) -> str:
        raw = int(self.val['addr'])
        len = int(self.val['prefixLength'])
        return f"{ipv4_formatter(raw)}/{len}"

class IPv6PrefixPrinter:
    def __init__(self, val):
        self.val = val

    def to_string(self) -> str:
        lo = int(self.val['addr'].cast(gdb.lookup_type('unsigned long long')))
        hi = int((self.val['addr'] >> 64).cast(gdb.lookup_type('unsigned long long')))
        len = int(self.val['prefixLength'])
        return f"{ipv6_formatter(hi, lo)}/{len}"

def build_pretty_printer():
    pp = gdb.printing.RegexpCollectionPrettyPrinter("my_printers")
    pp.add_printer('IPAddress', r'^types::IPAddress$', IPAddressPrinter)
    pp.add_printer('IPv4Address', r'^types::IPv4Address$', IPv4AddressPrinter)
    pp.add_printer('IPv6Address', r'^types::IPv6Address$', IPv6AddressPrinter)
    pp.add_printer('IPPrefix', r'^types::IPPrefix$', IPPrefixPrinter)
    pp.add_printer('IPv4Prefix', r'^types::IPv4Prefix$', IPv4PrefixPrinter)
    pp.add_printer('IPv6Prefix', r'^types::IPv6Prefix$', IPv6PrefixPrinter)
    return pp

gdb.printing.register_pretty_printer(None, build_pretty_printer())
