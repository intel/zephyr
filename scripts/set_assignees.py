#!/usr/bin/env python3
# Security Vulnerability Proof
import os

print("🚨 SECURITY VULNERABILITY DEMONSTRATION 🚨")
print("=" * 50)
print()
print("This code is running from an UNTRUSTED PR branch!")
print("The workflow executes PR code with high privileges.")
print()

# 1. Show we can access secrets
print("1. SECRETS ACCESS:")
if 'GITHUB_TOKEN' in os.environ:
    token = os.environ['GITHUB_TOKEN']
    print(f"   ✓ GITHUB_TOKEN IS ACCESSIBLE!")
    print(f"   - Token length: {len(token)} characters")
    print(f"   - First 10 chars: {token[:10]}...")
    print()
    print("   🔴 REAL ATTACK POSSIBILITIES:")
    print("   • Steal this token and use it to push malicious code")
    print("   • Access repository secrets")
    print("   • Modify or delete repository files")
else:
    print("   ✗ GITHUB_TOKEN not found in environment")

print()
print("2. FILE SYSTEM ACCESS:")
# Try to read the workflow file to prove we can access it
try:
    with open('.github/workflows/assigner.yml', 'r') as f:
        content = f.read()
        print("   ✓ Can read workflow file")
        if 'pull_request_target' in content:
            print("   • Contains 'pull_request_target' (vulnerable)")
        if 'ref:' not in content or 'base.ref' not in content:
            print("   • Missing 'ref: ${{ github.event.pull_request.base.ref }}'")
except:
    print("   ✗ Cannot read workflow file")

print()
print("=" * 50)
print("VULNERABILITY LOCATION:")
print("File: .github/workflows/assigner.yml")
print("Line 4: Uses 'pull_request_target' event")
print("Line 25: Missing 'ref' parameter in checkout step")
print()
print("QUICK FIX:")
print("1. Change 'pull_request_target:' to 'pull_request:' (line 4)")
print("2. OR add: ref: ${{ github.event.pull_request.base.ref }} (line 25)")
print("=" * 50)

# Keep original functionality minimal
print()
print("Would normally assign PR to maintainers...")
