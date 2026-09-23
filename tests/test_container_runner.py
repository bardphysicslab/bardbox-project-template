"""Verify archive isolation and failure propagation without requiring Docker."""
import json
import os
from pathlib import Path
import subprocess
import sys

RUNNER=Path(__file__).resolve().parents[1]/'scripts'/'validate_container.sh'


def prepare(tmp_path):
    repo=tmp_path/'repo';repo.mkdir();state=tmp_path/'state';state.mkdir();bin_dir=tmp_path/'bin';bin_dir.mkdir()
    def git(*args):
        return subprocess.run(['git',*args],cwd=repo,check=True,capture_output=True,text=True).stdout.strip()
    git('init');git('config','user.email','test@example.invalid');git('config','user.name','Fixture')
    (repo/'.devcontainer').mkdir();(repo/'.devcontainer'/'Dockerfile').write_text('FROM example\n')
    (repo/'README.md').write_text('first revision')
    git('add','.');git('commit','-m','first');first=git('rev-parse','HEAD')
    (repo/'README.md').write_text('second revision');git('commit','-am','second')
    (repo/'private-untracked.txt').write_text('must never enter archive')
    docker=bin_dir/'docker'
    docker.write_text('#!'+sys.executable+'\n'+'''import json,os,pathlib,sys,tarfile
state=pathlib.Path(os.environ['VALIDATION_TEST_STATE']);args=sys.argv[1:]
if args[0] in ('build','run'):
    files={}
    with tarfile.open(fileobj=sys.stdin.buffer,mode='r|') as archive:
        for item in archive:
            if item.isfile():files[item.name]=archive.extractfile(item).read().decode()
    (state/(args[0]+'.json')).write_text(json.dumps({'files':files,'args':args}))
    if args[0]=='build':pathlib.Path(args[args.index('--iidfile')+1]).write_text('sha256:fixture')
    else:sys.exit(int(os.environ.get('VALIDATION_TEST_EXIT','0')))
elif args[0]=='image':print('Image: sha256:fixture Platform: linux/test')
else:sys.exit(99)
''');docker.chmod(0o755)
    env={**os.environ,'PATH':str(bin_dir)+os.pathsep+os.environ['PATH'],'VALIDATION_TEST_STATE':str(state)}
    return repo,state,env,first


def test_only_selected_committed_source_enters_container(tmp_path):
    repo,state,env,first=prepare(tmp_path)
    result=subprocess.run(['bash',str(RUNNER),'HEAD~1'],cwd=repo,env=env,capture_output=True,text=True)
    assert result.returncode==0 and first in result.stdout
    assert 'local changes are excluded' in result.stderr
    build=json.loads((state/'build.json').read_text());run=json.loads((state/'run.json').read_text())
    assert set(build['files'])=={'.devcontainer/Dockerfile'}
    assert run['files']['README.md']=='first revision'
    assert 'private-untracked.txt' not in run['files'] and '.git' not in run['files']
    assert '--rm' in run['args'] and '-v' not in run['args'] and '--mount' not in run['args']


def test_test_failure_propagates_and_invalid_revision_never_builds(tmp_path):
    repo,state,env,_=prepare(tmp_path)
    invalid=subprocess.run(['bash',str(RUNNER),'not-a-revision'],cwd=repo,env=env,capture_output=True)
    assert invalid.returncode!=0 and not list(state.iterdir())
    result=subprocess.run(['bash',str(RUNNER)],cwd=repo,env={**env,'VALIDATION_TEST_EXIT':'23'},capture_output=True)
    assert result.returncode==23
