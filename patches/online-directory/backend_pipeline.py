"""Compose an owner-deployed API bundle over the pinned current library Worker."""
from pathlib import Path
import argparse,difflib,hashlib,json,shutil,zipfile
AUTH=Path(__file__).resolve().parent
ROOT=AUTH.parents[1]
API=ROOT/'build/library-workflow-20261023-r8/stage/PRIVATE-API/worker-single-file.mjs'
EVIDENCE=ROOT/'artifacts/DKR-R-Public-Lobbies-Plan-20261024-r1/EVIDENCE.json'
def sha(raw):return hashlib.sha256(raw).hexdigest()
def once(text,old,new):
    if text.count(old)!=1:raise ValueError('API entry point changed: '+old[:100])
    return text.replace(old,new,1)
def compose(output):
    if output.exists():raise ValueError('Choose a fresh owner bundle directory')
    evidence=json.loads(EVIDENCE.read_text(encoding='utf-8'))
    original=API.read_bytes()
    expected=evidence['reviewed_inputs_sha256'][API.as_posix()]
    if sha(original)!=expected:raise ValueError('The approved local API baseline changed; rebase before composing')
    worker=original.decode('utf-8')
    worker=once(worker,'export default {\n  async fetch(request, env, ctx) {',
        (AUTH/'backend/gateway.inl').read_text(encoding='utf-8')+'\nexport default {\n  async fetch(request, env, ctx) {')
    anchor='''  async fetch(request, env, ctx) {
    let response;
    try {
      const url = new URL(request.url);'''
    worker=once(worker,anchor,anchor+'''
      const onlineResponse = await handleOnlineDirectory(request, env);
      if (onlineResponse) return onlineResponse;''')
    output.mkdir(parents=True)
    (output/'PRIVATE-API').mkdir();(output/'ONLINE-WORKER').mkdir();(output/'ROLLBACK').mkdir()
    (output/'PRIVATE-API/worker-single-file.mjs').write_text(worker,encoding='utf-8',newline='\n')
    (output/'ROLLBACK/worker-single-file.mjs').write_bytes(original)
    (output/'REVIEW.patch').write_text(''.join(difflib.unified_diff(original.decode('utf-8').splitlines(True),worker.splitlines(True),
        fromfile='before/worker-single-file.mjs',tofile='after/worker-single-file.mjs')),encoding='utf-8')
    for name in ('directory.mjs','wrangler-online.jsonc'):shutil.copy2(AUTH/'backend'/name,output/'ONLINE-WORKER'/name)
    for name in ('DEPLOYMENT.md','PLAYTEST.txt','IMPLEMENTATION.txt','QA.md'):shutil.copy2(AUTH/name,output/name)
    (output/'PRIVATE-API/BINDINGS-TO-MERGE.jsonc').write_text(json.dumps({
        'services':[{'binding':'ONLINE_DIRECTORY','service':'dkrr-online-directory'}],
        'vars':{'ONLINE_ENABLED':'false','RUNNING_SPECTATORS_ENABLED':'false'}},indent=2)+'\n',encoding='utf-8')
    receipt={'schema':1,'feature':'dkrr-online-directory-v1','published':False,
        'source_api_sha256':expected,'api_baseline':'Local library-workflow r8 bundle reviewed in the approved plan',
        'authoring_sha256':{p.relative_to(AUTH).as_posix():sha(p.read_bytes()) for p in sorted(AUTH.rglob('*')) if p.is_file() and '__pycache__' not in p.parts},
        'files':{p.relative_to(output).as_posix():sha(p.read_bytes()) for p in sorted(output.rglob('*')) if p.is_file()}}
    (output/'BUNDLE-RECEIPT.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')
    print('Owner API bundle composed; no FTP, database or service deployment performed:',output)
def verify(output):
    receipt=json.loads((output/'BUNDLE-RECEIPT.json').read_text(encoding='utf-8'))
    for name,digest in receipt['files'].items():
        if sha((output/name).read_bytes())!=digest:raise ValueError('Bundle file changed: '+name)
    if receipt['published']:raise ValueError('Unexpected deployment claim')
    print('Owner bundle hashes verified.')
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);p.add_argument('--archive',type=Path);p.add_argument('--verify',action='store_true');a=p.parse_args()
    if not a.verify:compose(a.output)
    verify(a.output)
    if a.archive:
        if a.archive.exists():raise ValueError('Choose a fresh bundle archive')
        a.archive.parent.mkdir(parents=True,exist_ok=True)
        with zipfile.ZipFile(a.archive,'w',zipfile.ZIP_DEFLATED) as archive:
            for path in sorted(a.output.rglob('*')):
                if path.is_file():archive.write(path,path.relative_to(a.output).as_posix())
        print('Owner bundle archive:',a.archive,sha(a.archive.read_bytes()))
