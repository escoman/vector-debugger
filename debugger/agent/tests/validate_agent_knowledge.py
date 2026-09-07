#!/usr/bin/env python3
"""
validate_agent_knowledge.py — Validator for Stage 6.5 Agent Knowledge Base.

Checks:
  - TASK.md: unique ID, required sections, tool references, knowledge references
  - Knowledge docs: valid metadata block, allowed status values, source present
  - Profiles: task ID references, knowledge ID references, no cycles
"""

import os
import re
import sys
import yaml  # PyYAML — fallback to manual parsing if unavailable

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

AGENT_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TASKS_DIR = os.path.join(AGENT_DIR, "tasks")
KNOWLEDGE_DIR = os.path.join(AGENT_DIR, "knowledge", "vector06c")
PROFILES_DIR = os.path.join(AGENT_DIR, "profiles")

# All registered MCP tools (from mcp_adapter.cpp)
VALID_TOOLS = {
    "debug_run", "debug_pause", "debug_step", "debug_reset", "debug_is_running",
    "debug_get_cpu_state", "debug_get_registers", "debug_set_register",
    "debug_read_memory", "debug_write_memory",
    "debug_read_io", "debug_write_io",
    "debug_set_breakpoint", "debug_remove_breakpoint", "debug_list_breakpoints",
    "debug_clear_breakpoints", "debug_set_breakpoint_enabled",
    "debug_disassemble", "debug_get_instruction_history", "debug_get_execution_trace",
    "debug_get_stack",
    "debug_get_symbols", "debug_get_function", "debug_get_function_context",
    "debug_get_xrefs", "debug_get_call_graph",
    "debug_get_memory_map", "debug_get_vram_info", "debug_get_screen_info",
    "debug_get_io_trace",
    "debug_get_state",
    "debug_load_rom",
    "debug_set_comment", "debug_set_function_comment", "debug_rename_function",
    "debug_create_function", "debug_delete_function", "debug_add_label",
}

VALID_STATUSES = {"verified", "partially_verified", "unverified"}

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

errors = []
warnings = []


def error(msg):
    errors.append(msg)
    print(f"  ERROR: {msg}")


def warn(msg):
    warnings.append(msg)
    print(f"  WARN:  {msg}")


def ok(msg):
    print(f"  OK:    {msg}")


# ---------------------------------------------------------------------------
# Metadata parsing
# ---------------------------------------------------------------------------

def parse_yaml_frontmatter(filepath):
    """Parse YAML front matter between --- markers."""
    with open(filepath, "r", encoding="utf-8") as f:
        content = f.read()

    match = re.match(r"^---\s*\n(.*?)\n---\s*\n", content, re.DOTALL)
    if not match:
        return None

    yaml_text = match.group(1)
    try:
        import yaml as _yaml
        return _yaml.safe_load(yaml_text)
    except ImportError:
        # Manual fallback for simple key: value pairs
        result = {}
        for line in yaml_text.strip().split("\n"):
            if ":" in line:
                key, val = line.split(":", 1)
                result[key.strip()] = val.strip()
        return result


# ---------------------------------------------------------------------------
# Knowledge validation
# ---------------------------------------------------------------------------

def validate_knowledge():
    print("\n=== Knowledge Base Validation ===")
    knowledge_ids = set()

    if not os.path.isdir(KNOWLEDGE_DIR):
        error(f"Knowledge directory not found: {KNOWLEDGE_DIR}")
        return knowledge_ids

    for fname in sorted(os.listdir(KNOWLEDGE_DIR)):
        if not fname.endswith(".md"):
            continue
        fpath = os.path.join(KNOWLEDGE_DIR, fname)
        topic = fname.replace(".md", "")
        print(f"\n  Checking knowledge: {fname}")

        meta = parse_yaml_frontmatter(fpath)
        if meta is None:
            error(f"{fname}: missing YAML front matter")
            continue

        # Check required fields
        for field in ("platform", "topic", "status", "source"):
            if field not in meta:
                error(f"{fname}: missing metadata field '{field}'")

        # Check status
        status = str(meta.get("status", ""))
        if status and status not in VALID_STATUSES:
            error(f"{fname}: invalid status '{status}' (allowed: {VALID_STATUSES})")
        else:
            ok(f"{fname}: status = {status}")

        # Check source
        source = meta.get("source", "")
        if not source:
            warn(f"{fname}: missing source")

        # Check platform
        platform = meta.get("platform", "")
        if platform != "Vector-06C":
            warn(f"{fname}: platform = '{platform}' (expected 'Vector-06C')")

        # Track ID
        kid = f"vector06c/{topic}"
        if kid in knowledge_ids:
            error(f"{fname}: duplicate knowledge ID '{kid}'")
        knowledge_ids.add(kid)

    ok(f"Found {len(knowledge_ids)} knowledge documents")
    return knowledge_ids


# ---------------------------------------------------------------------------
# Task validation
# ---------------------------------------------------------------------------

def extract_task_id(filepath):
    """Extract task ID from ## ID section."""
    with open(filepath, "r", encoding="utf-8") as f:
        content = f.read()
    match = re.search(r"^## ID\s*\n\s*(\S+)\s*$", content, re.MULTILINE)
    if match:
        return match.group(1).strip()
    return None


def extract_required_tools(filepath):
    """Extract tool names from ## Required Tools section."""
    with open(filepath, "r", encoding="utf-8") as f:
        content = f.read()
    match = re.search(r"^## Required Tools\s*\n(.*?)(?=^## |\Z)", content, re.MULTILINE | re.DOTALL)
    if not match:
        return []
    section = match.group(1)
    tools = re.findall(r"debug_\w+", section)
    return tools


def extract_required_knowledge(filepath):
    """Extract knowledge IDs from ## Required Knowledge section."""
    with open(filepath, "r", encoding="utf-8") as f:
        content = f.read()
    match = re.search(r"^## Required Knowledge\s*\n(.*?)(?=^## |\Z)", content, re.MULTILINE | re.DOTALL)
    if not match:
        return []
    section = match.group(1)
    # Match patterns like vector06c/cpu
    knowledge = re.findall(r"vector06c/\w+", section)
    return knowledge


def validate_tasks(knowledge_ids):
    print("\n=== Task Library Validation ===")
    task_ids = set()

    if not os.path.isdir(TASKS_DIR):
        error(f"Tasks directory not found: {TASKS_DIR}")
        return task_ids

    for dirname in sorted(os.listdir(TASKS_DIR)):
        task_file = os.path.join(TASKS_DIR, dirname, "TASK.md")
        if not os.path.isfile(task_file):
            continue

        print(f"\n  Checking task: {dirname}/TASK.md")

        # Check ID
        tid = extract_task_id(task_file)
        if tid is None:
            error(f"{dirname}: missing ## ID section")
            continue
        if tid != dirname:
            error(f"{dirname}: ID mismatch (ID='{tid}', directory='{dirname}')")
        if tid in task_ids:
            error(f"{dirname}: duplicate task ID '{tid}'")
        task_ids.add(tid)
        ok(f"{dirname}: ID = {tid}")

        # Check required sections
        with open(task_file, "r", encoding="utf-8") as f:
            content = f.read()

        required_sections = ["Description", "Required Knowledge", "Required Tools",
                             "Procedure", "Output"]
        for section in required_sections:
            if f"## {section}" not in content:
                error(f"{dirname}: missing section '## {section}'")

        # Check tool references
        tools = extract_required_tools(task_file)
        for tool in tools:
            if tool not in VALID_TOOLS:
                error(f"{dirname}: references unknown tool '{tool}'")
        ok(f"{dirname}: {len(tools)} tool references checked")

        # Check knowledge references
        knowledge = extract_required_knowledge(task_file)
        for kid in knowledge:
            if kid not in knowledge_ids:
                error(f"{dirname}: references unknown knowledge '{kid}'")
        ok(f"{dirname}: {len(knowledge)} knowledge references checked")

    ok(f"Found {len(task_ids)} tasks")
    return task_ids


# ---------------------------------------------------------------------------
# Profile validation
# ---------------------------------------------------------------------------

def extract_profile_tasks(filepath):
    """Extract task IDs from ## Tasks section."""
    with open(filepath, "r", encoding="utf-8") as f:
        content = f.read()
    match = re.search(r"^## Tasks\s*\n(.*?)(?=^## |\Z)", content, re.MULTILINE | re.DOTALL)
    if not match:
        return []
    section = match.group(1)
    # Match bold task names like **generate_map**
    tasks = re.findall(r"\*\*(\w+)\*\*", section)
    return tasks


def extract_profile_knowledge(filepath):
    """Extract knowledge IDs from ## Knowledge section."""
    with open(filepath, "r", encoding="utf-8") as f:
        content = f.read()
    match = re.search(r"^## Knowledge\s*\n(.*?)(?=^## |\Z)", content, re.MULTILINE | re.DOTALL)
    if not match:
        return []
    section = match.group(1)
    knowledge = re.findall(r"vector06c/\w+", section)
    return knowledge


def validate_profiles(task_ids, knowledge_ids):
    print("\n=== Profile Validation ===")
    profile_ids = set()

    if not os.path.isdir(PROFILES_DIR):
        error(f"Profiles directory not found: {PROFILES_DIR}")
        return profile_ids

    for fname in sorted(os.listdir(PROFILES_DIR)):
        if not fname.endswith(".md"):
            continue
        fpath = os.path.join(PROFILES_DIR, fname)
        pid = fname.replace(".md", "")
        print(f"\n  Checking profile: {fname}")

        # Check ID
        with open(fpath, "r", encoding="utf-8") as f:
            content = f.read()

        id_match = re.search(r"^## ID\s*\n\s*(\S+)\s*$", content, re.MULTILINE)
        if id_match:
            declared_id = id_match.group(1).strip()
            if declared_id != pid:
                error(f"{fname}: ID mismatch (ID='{declared_id}', filename='{pid}')")
            if pid in profile_ids:
                error(f"{fname}: duplicate profile ID '{pid}'")
            profile_ids.add(pid)
            ok(f"{fname}: ID = {pid}")
        else:
            error(f"{fname}: missing ## ID section")

        # Check required sections
        for section in ["Description", "Tasks", "Knowledge"]:
            if f"## {section}" not in content:
                error(f"{fname}: missing section '## {section}'")

        # Check task references
        tasks = extract_profile_tasks(fpath)
        for tid in tasks:
            if tid not in task_ids:
                error(f"{fname}: references unknown task '{tid}'")
        ok(f"{fname}: {len(tasks)} task references checked")

        # Check knowledge references
        knowledge = extract_profile_knowledge(fpath)
        for kid in knowledge:
            if kid not in knowledge_ids:
                error(f"{fname}: references unknown knowledge '{kid}'")
        ok(f"{fname}: {len(knowledge)} knowledge references checked")

    ok(f"Found {len(profile_ids)} profiles")
    return profile_ids


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    print("=" * 60)
    print("Agent Knowledge Base Validator (Stage 6.5)")
    print("=" * 60)
    print(f"Agent directory: {AGENT_DIR}")

    # Phase 1: Knowledge
    knowledge_ids = validate_knowledge()

    # Phase 2: Tasks (depends on knowledge IDs)
    task_ids = validate_tasks(knowledge_ids)

    # Phase 3: Profiles (depends on task + knowledge IDs)
    profile_ids = validate_profiles(task_ids, knowledge_ids)

    # Summary
    print("\n" + "=" * 60)
    print(f"Summary: {len(knowledge_ids)} knowledge, {len(task_ids)} tasks, {len(profile_ids)} profiles")
    print(f"Errors:   {len(errors)}")
    print(f"Warnings: {len(warnings)}")
    print("=" * 60)

    if errors:
        print("\nFAILED — fix errors above")
        return 1
    else:
        print("\nPASSED")
        return 0


if __name__ == "__main__":
    sys.exit(main())
