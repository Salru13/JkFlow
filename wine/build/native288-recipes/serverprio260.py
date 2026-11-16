#!/usr/bin/env python3
"""Relink wineserver with an optional, env-gated main-loop priority.

Physical waitprobe260 under CPU load: timers fire 15-40 ms late and GetTickCount
steps by 30-100 ms, because the server thread that fires every timeout is
scheduled late. PW_SERVER_PRIO=<256..767> sets that thread's Orbis priority
(a lower number is a higher priority). Unset means no call is made, so the
default path keeps its current behaviour.

Run in WSL. Output: $JKFLOW_BUILD_ROOT/native288-serverprio260/.
"""
import json
import os  # JKFLOW_BUILD_ROOT
import shutil
import subprocess
from pathlib import Path

BASE = Path(os.environ['JKFLOW_BUILD_ROOT'])
REF = BASE / 'native266-wineserver-r1'
OUT = BASE / 'native288-serverprio260'
CWD = BASE / 'native248-build-wine/build'
ANCHOR = '''    set_current_time();
    server_start_time = current_time;

    main_loop_epoll();'''
INSERT = '''#if defined(WINE_INPROCESS_SERVER) && defined(__PROSPERO__)
    {
        extern int scePthreadSetprio( void *thread, int priority );
        extern int scePthreadGetprio( void *thread, int *priority );
        extern void *scePthreadSelf( void );
        const char *text = getenv( "PW_SERVER_PRIO" );
        int wanted = text ? atoi( text ) : 0, before = -1, after = -1, status = -1;
        if (wanted >= 256 && wanted <= 767)
        {
            scePthreadGetprio( scePthreadSelf(), &before );
            status = scePthreadSetprio( scePthreadSelf(), wanted );
            scePthreadGetprio( scePthreadSelf(), &after );
            fprintf( stderr, "wine-ps5: server main-loop priority %d -> %d status=%#x\\n", before, after, status );
        }
    }
#endif
'''


def writable(path):
    data = Path(path).read_bytes()
    offset = int.from_bytes(data[32:40], 'little')
    size = int.from_bytes(data[54:56], 'little')
    count = int.from_bytes(data[56:58], 'little')
    rows = []
    for i in range(count):
        p = data[offset + i * size:offset + (i + 1) * size]
        if int.from_bytes(p[0:4], 'little') == 1 and int.from_bytes(p[4:8], 'little') & 2:
            rows.append((int.from_bytes(p[16:24], 'little'), int.from_bytes(p[40:48], 'little')))
    return rows


def main():
    receipt = json.loads((REF / 'receipt.json').read_text())
    commands = {row['name']: row['command'] for row in receipt['commands']}
    shutil.rmtree(OUT, ignore_errors=True)  # only this script's own output
    (OUT / 'server').mkdir(parents=True)

    source = (REF / 'server/fd.c').read_text()
    newline = '\r\n' if ANCHOR.replace('\n', '\r\n') in source else '\n'
    anchor = ANCHOR.replace('\n', newline)
    assert source.count(anchor) == 1, 'anchor must match exactly once'
    head, tail = anchor.split(newline + newline)
    source = source.replace(anchor, head + newline + INSERT.replace('\n', newline) + newline + tail)
    fd_c = OUT / 'server/fd.c'
    fd_c.write_text(source)

    compile_cmd = [str(fd_c) if a == str(REF / 'server/fd.c') else str(OUT / 'fd.o') if a == str(REF / 'fd.o') else a
                   for a in commands['compile']]
    assert str(OUT / 'fd.o') in compile_cmd and str(fd_c) in compile_cmd
    subprocess.run(compile_cmd, check=True, cwd=CWD)

    link_cmd = [str(OUT / 'fd.o') if a == str(REF / 'fd.o')
                else str(OUT / 'candidate.elf') if a == str(REF / 'candidate.elf') else a
                for a in commands['candidate-link']]
    assert str(OUT / 'fd.o') in link_cmd and str(OUT / 'candidate.elf') in link_cmd
    subprocess.run(link_cmd, check=True, cwd=CWD)

    print('linked', OUT / 'candidate.elf')


if __name__ == '__main__':
    main()
