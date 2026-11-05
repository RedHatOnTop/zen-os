import json
with open('reports/gate-results.jsonl') as f:
    for i, line in enumerate(f, 1):
        line = line.strip()
        if not line:
            continue
        try:
            d = json.loads(line)
            print(f"Line {i}: gate={d['gate']}, status={d['status']}")
        except Exception as e:
            print(f"Line {i}: PARSE ERROR: {e}")
