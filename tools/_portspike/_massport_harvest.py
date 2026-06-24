#!/usr/bin/env python3
# One-shot: harvest structured closure results from the (interrupted) workflow
# transcripts so we can synthesize the mass-port plan without re-running verify.
import json, glob, os, re

D = (r"C:/Users/gary/.claude/projects/D--sonicmaniasaturn/"
     r"ff0d80a5-2843-4cd7-ab54-1f697806cfd9/subagents/workflows/wf_2bc8e33b-835")
research = {}
verify = {}


def walk(o):
    if isinstance(o, dict):
        if 'object' in o and ('final_verdict' in o or 'integration_recipe' in o or 'missed_symbols' in o):
            verify[o['object']] = o
        elif 'object' in o and ('verdict' in o or 'closure' in o):
            research.setdefault(o['object'], o)
        for v in o.values():
            walk(v)
    elif isinstance(o, list):
        for v in o:
            walk(v)
    elif isinstance(o, str):
        s = o.strip()
        if s.startswith('{') and ('verdict' in s or 'closure' in s):
            try:
                walk(json.loads(s))
            except Exception:
                for m in re.findall(r'\{.*?"object".*?\}', s):
                    try:
                        walk(json.loads(m))
                    except Exception:
                        pass


for f in glob.glob(os.path.join(D, "*.jsonl")):
    try:
        for line in open(f, encoding='utf-8', errors='replace'):
            try:
                walk(json.loads(line))
            except Exception:
                pass
    except Exception:
        pass

print("research specs:", len(research), sorted(research))
print("verify specs  :", len(verify), sorted(verify))
print("\n=== MERGED (verify preferred) ===")
allobj = sorted(set(research) | set(verify))
out = {}
for ob in allobj:
    rec = verify.get(ob) or research.get(ob) or {}
    out[ob] = {
        'verdict': rec.get('final_verdict') or rec.get('verdict'),
        'chain': rec.get('chain_objects') or [],
        'agrees': rec.get('agrees'),
        'missed': [m.get('symbol') for m in (rec.get('missed_symbols') or []) if isinstance(m, dict)],
        'verified': ob in verify,
    }
    print("  %-16s verdict=%-13s verified=%s chain=%s missed=%s"
          % (ob, str(out[ob]['verdict']), out[ob]['verified'], out[ob]['chain'], out[ob]['missed']))
missing = [o for o in [
    'Decoration', 'Water', 'ForceSpin', 'SpinBooster', 'TimeAttackGate', 'CorkscrewPath', 'ForceUnstick',
    'Newtron', 'BuzzBomber', 'Chopper', 'Crabmeat', 'Motobug', 'Batbrain',
    'BadnikHelpers', 'Explosion', 'Animals', 'ScoreBonus',
    'Platform', 'PlatformControl', 'PlatformNode', 'ItemBox', 'Crate', 'Debris', 'InvincibleStars',
    'BreakableWall', 'CollapsingPlatform'] if o not in allobj]
print("\nNO RESULT harvested for:", missing)
open(os.path.join('tools', '_portspike', '_massport_harvest.json'), 'w').write(
    json.dumps({'research': research, 'verify': verify, 'merged': out, 'missing': missing}, indent=1))
print("wrote tools/_portspike/_massport_harvest.json")
