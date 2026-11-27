# Boots the image in W (flash.bin and cf.img) on the model and runs shell commands:
#   STACKETTE=<the stackette repository> python3 runmodel.py W cmd [cmd...]
import os
import sys
sys.path.insert(0, os.environ['STACKETTE'])
from system_model import SystemModel, run
from ide_model import CFCard

SEC = 512


def boot(w):
    m = SystemModel(open(w + '/flash.bin', 'rb').read())
    img = open(w + '/cf.img', 'rb').read()
    card = CFCard(sectors=len(img) // SEC)
    card.image[:] = img
    m.ide_dev = card
    return m


def feed(m, text, chunk=60000):
    for ch in text:
        m.uart_feed(ch.encode())
        run(m, chunk)


def wait_for(m, marker, limit=400000000, chunk=500000):
    start = m.steps
    seen = 0
    while m.steps - start < limit:
        run(m, chunk)
        b = bytes(m.out)
        if len(b) > seen:
            print(b[seen:].decode('latin1').replace('\r', ''), end='', flush=True)
            seen = len(b)
        if marker in b:
            return True
    return False


def main():
    w = sys.argv[1]
    m = boot(w)
    assert wait_for(m, b'#', 20000000) or True
    run(m, 6000000)
    feed(m, 'root\r')
    run(m, 4000000)
    print(bytes(m.out).decode('latin1').replace('\r', ''))
    for cmd in sys.argv[2:]:
        m.out.clear()
        feed(m, cmd + '; echo rc=$?\r')
        ok = wait_for(m, b'\nrc=')
        run(m, 300000)
        print(bytes(m.out).decode('latin1').replace('\r', ''), flush=True)
        print(cmd, 'done' if ok else 'TIMEOUT', 'steps', m.steps, flush=True)


main()
