"""Adapt native251's reason profiling without thread-data/TLS ABI growth."""
import pathlib,hashlib,difflib,json,argparse
p=argparse.ArgumentParser();p.add_argument('--source',type=pathlib.Path,required=True);p.add_argument('--out',type=pathlib.Path,required=True);a=p.parse_args()
here=pathlib.Path(__file__).resolve().parent;old=a.source.read_text();new=old
assert hashlib.sha256(a.source.read_bytes()).hexdigest()=='86f44762d8b53a32e918a7f5324b8016f868143066a6dee548987f739a8b8487'
def change(x,y):
 global new
 assert new.count(x)==1,(x[:80],new.count(x));new=new.replace(x,y)
head='static unsigned int server_try_shared_sync_inprocess(HANDLE handle, unsigned int operation, unsigned int count,\n                                    const LARGE_INTEGER *timeout, unsigned int *previous)'
change(head,(here/'native265-shared-diagnostic.inc').read_text()+'\n'+head[:-1]+', unsigned int *why)')
change('''    if (!data->tid || operation > PW_SYNC_RELEASE ||
        !pw_sync_backend_ready(shared_sync_backend)) return STATUS_NOT_IMPLEMENTED;''','''    if (why) *why = N265_HIT;
    if (!data->tid || operation > PW_SYNC_RELEASE)
    { if (why) *why = N265_CONTEXT; return STATUS_NOT_IMPLEMENTED; }
    if (!shared_sync_backend)
    { if (why) *why = N265_NOAPI; return STATUS_NOT_IMPLEMENTED; }
    if (!pw_sync_backend_ready(shared_sync_backend))
    { if (why) *why = N265_NOTREADY; return STATUS_NOT_IMPLEMENTED; }''')
change('    if (value == PW_SYNC_SLOT_NEGATIVE) return STATUS_NOT_IMPLEMENTED;','    if (value == PW_SYNC_SLOT_NEGATIVE) { if (why) *why = N265_NEGATIVE; return STATUS_NOT_IMPLEMENTED; }')
change('''    if (!value || value == PW_SYNC_SLOT_NEGATIVE ||
        !pw_sync_backend_ready(shared_sync_backend)) return STATUS_NOT_IMPLEMENTED;''','''    if (!value || value == PW_SYNC_SLOT_NEGATIVE || !pw_sync_backend_ready(shared_sync_backend))
    {
        if (why) *why = !value ? N265_LOOKUP : value == PW_SYNC_SLOT_NEGATIVE ? N265_NEGATIVE : N265_NOTREADY;
        return STATUS_NOT_IMPLEMENTED;
    }''')
for bit in ['SYNCHRONIZE','MODIFY']:
 change(f'        if (!(value & PW_SYNC_SLOT_{bit})) return STATUS_NOT_IMPLEMENTED;',f'        if (!(value & PW_SYNC_SLOT_{bit})) {{ if (why) *why = N265_ACCESS; return STATUS_NOT_IMPLEMENTED; }}')
change('        if (!pw_sync_word_try_wait(word)) return STATUS_NOT_IMPLEMENTED;','        if (!pw_sync_word_try_wait(word)) { if (why) *why = N265_WORD; return STATUS_NOT_IMPLEMENTED; }')
change('            if (!pw_sync_word_try_release(word, count, &old)) return STATUS_NOT_IMPLEMENTED;','            if (!pw_sync_word_try_release(word, count, &old)) { if (why) *why = N265_WORD; return STATUS_NOT_IMPLEMENTED; }')
change('''        else if (!pw_sync_word_try_event(word, operation == PW_SYNC_SET, &old))
            return STATUS_NOT_IMPLEMENTED;''','''        else if (!pw_sync_word_try_event(word, operation == PW_SYNC_SET, &old))
            { if (why) *why = N265_WORD; return STATUS_NOT_IMPLEMENTED; }''')
change('    return server_try_shared_sync_inprocess(handle, operation, count, timeout, previous);','''    unsigned int result, why = N265_HIT;
    struct n265_shared_stats *stat = n265_gate() && operation < 4 ? n265_slot() : NULL;
    int profile = stat && !__atomic_exchange_n(&stat->busy, 1, __ATOMIC_ACQUIRE);
    result = server_try_shared_sync_inprocess(handle, operation, count, timeout, previous, profile ? &why : NULL);
    if (profile) n265_record(stat, operation, why);
    return result;''')
change('''    int (*connect_server)( const char *, char **, const char * );
    int fd;''','''    int (*connect_server)( const char *, char **, const char * );
    int fd, sync_requested = -1, sync_query_present = 0, sync_api_valid = 0;''')
change('    if (call_server_direct && server_shared_sync_enabled())','    if (call_server_direct && (sync_requested = server_shared_sync_enabled()))')
change('''        if (query && pw_sync_backend_valid(api = query(PW_SYNC_BACKEND_VERSION)))
            shared_sync_backend = api;''','''        sync_query_present = !!query;
        if (query && (sync_api_valid = pw_sync_backend_valid(api = query(PW_SYNC_BACKEND_VERSION))))
            shared_sync_backend = api;''')
anchor='    fprintf(stderr, "wine-ps5: shared event/semaphore backend: %s\\n", shared_sync_backend ? "on" : "off");'
change(anchor,'''    if (n265_gate())
        ERR("PW_SYNC265_SELECT direct=%u requested=%d query=%d abi=%d selected=%u ready=%u\\n",
            !!call_server_direct, sync_requested, sync_query_present, sync_api_valid,
            !!shared_sync_backend, !!pw_sync_backend_ready(shared_sync_backend));
'''+anchor)
a.out.mkdir(parents=True,exist_ok=False);(a.out/'server.c').write_text(new)
patch=''.join(difflib.unified_diff(old.splitlines(True),new.splitlines(True),fromfile='a/dlls/ntdll/unix/server.c',tofile='b/dlls/ntdll/unix/server.c'))
(a.out/'candidate.patch').write_text(patch)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
receipt={'input':str(a.source),'input_sha256':sha(a.source),'candidate_sha256':sha(a.out/'server.c'),'patch_sha256':sha(a.out/'candidate.patch'),'scope':'diagnostics only; default off; no baseline/source mutation'}
(a.out/'prepare.json').write_text(json.dumps(receipt,indent=2));print(json.dumps(receipt,indent=2))
