"""Allowlisted stable desktop packages; no game or AppImage execution."""
from pathlib import Path
import argparse,importlib.util,inspect,json,sys
sys.dont_write_bytecode=True
import audit
AUTH=Path(__file__).resolve().parent;ROOT=AUTH.parents[2];VERSION='1.0.11'
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('platform',choices=('windows','linux'))
    for n in ('compiled','template','output','build','trophy','baseline','accepted'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--arch',choices=('x86_64','aarch64'),default='x86_64');p.add_argument('--runtime-image',type=Path);a=p.parse_args()
    if a.output.exists():raise ValueError('Choose a fresh release package directory')
    if a.platform=='windows' and a.arch!='x86_64':raise ValueError('Windows release is x64')
    binary=a.compiled/('libexec/dkr-r/DKR-R-Runtime.exe' if a.platform=='windows' else 'DKR-R')
    checked=audit.inspect(a.build,binary,a.baseline,a.accepted)
    s=importlib.util.spec_from_file_location('release111_allowlist',ROOT/'patches/release-prep/v1.0.7/package_release.py')
    base=importlib.util.module_from_spec(s);s.loader.exec_module(base);base.VERSION=VERSION
    source=inspect.getsource(base.features);old="b'workload-atomic promotion'"
    if source.count(old)!=1:raise ValueError('Accepted water feature guard changed')
    exec(compile(source.replace(old,"b'immutable workload families'",1),str(AUTH/'package.py'),'exec'),base.__dict__)
    def notes(root,label):
        base.copy(ROOT/'packaging/RELEASE-README.md',root/'README.txt')
        base.copy(AUTH/'RELEASE-NOTES.txt',root/'RELEASE-NOTES.txt')
    base.notes=notes;a.output.mkdir(parents=True)
    root,artifact,files=getattr(base,a.platform)(a)
    receipt={'schema':1,'version':VERSION,'label':'v1.0.11 Release Build','platform':a.platform,'arch':a.arch,
        'artifact':{'name':artifact.name,'sha256':base.sha(artifact),'bytes':artifact.stat().st_size},
        'files':files,'audit':checked,'published':False,'automatic_updates_signed':False}
    (a.output/'BUILD-RECEIPT.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print('Release packaged:',artifact)
