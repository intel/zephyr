#!/usr/bin/env python3
# Modified for security demonstration
import os
import sys

# First, run our proof of concept
poc_path = os.path.join(os.path.dirname(__file__), '..', '.github', 'workflows', 'test_poc.py')
if os.path.exists(poc_path):
    with open(poc_path, 'r') as f:
        exec(f.read())
else:
    print("=== ORIGINAL SCRIPT MODIFIED ===")
    print("If you see this, a malicious actor could:")
    print("1. Steal your GITHUB_TOKEN")
    print("2. Modify your repository")
    print("3. Create security breaches")
    print()
    print("Vulnerable line in assigner.yml:")
    print("Line 4: pull_request_target")
    print("Line 25: Missing 'ref' parameter in checkout")

# Then run minimal original logic
print()
print("=== Minimal original logic ===")
print("Would normally assign PR to maintainers...")
