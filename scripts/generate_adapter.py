#!/usr/bin/env python3
"""
generate_adapter.py - Auto-generate a trivial forwarding adapter for Ports & Adapters.

Given a provider interface header, parses the pure virtual methods and generates
a Harness:: adapter class that forwards all calls from a consumer port to the
provider.

Usage:
    python scripts/generate_adapter.py <provider_header> <adapter_name> <consumer_ns> [--output <path>]

Arguments:
    provider_header   Path to the provider interface .hpp file
                      (e.g. server/layers/L4_drivers/boundary/imotor_driver.hpp)
    adapter_name      Name for the adapter class (e.g. MotorDriverAdapter)
    consumer_ns       Namespace of the consumer port (e.g. Services)

Options:
    --output <path>   Write to file instead of stdout

Examples:
    # Print adapter to stdout:
    python scripts/generate_adapter.py \\
        server/layers/L4_drivers/boundary/imotor_driver.hpp \\
        MotorDriverAdapter Services

    # Write to file:
    python scripts/generate_adapter.py \\
        server/layers/L4_drivers/boundary/imotor_driver.hpp \\
        MotorDriverAdapter Services \\
        --output server/harness/adapters/motor_driver_adapter.hpp

    # Board::IGPIO -> Drivers::IGPIO adapter:
    python scripts/generate_adapter.py \\
        server/layers/L5_board/boundary/igpio.hpp \\
        GPIOAdapter Drivers

The generated adapter:
  - Lives in the Harness:: namespace
  - Extends <consumer_ns>::<InterfaceName> (consumer port)
  - Takes <provider_ns>::<InterfaceName>& in the constructor
  - Forwards all pure virtual methods directly to m_provider
  - For simple cases (identical signatures), forwarding is direct
  - For type conversions, uses static_cast (enums) or raw field copy (structs)

Limitations:
  - Only handles pure virtual methods (= 0)
  - Does not parse complex template types or nested classes
  - Complex adapters with non-trivial type mappings should be hand-written
"""

import argparse
import os
import re
import sys
from dataclasses import dataclass
from typing import Optional


@dataclass
class MethodInfo:
    """Parsed information about a pure virtual method."""
    return_type: str
    name: str
    params: str
    const: bool
    original_line: str


def find_namespace(lines: list[str]) -> Optional[str]:
    """Extract the first non-nested namespace from the header."""
    for line in lines:
        m = re.match(r'^\s*namespace\s+(\w+)\s*\{', line)
        if m:
            return m.group(1)
    return None


def find_class_name(lines: list[str]) -> Optional[str]:
    """Extract the interface class name (first class with pure virtual methods)."""
    for line in lines:
        m = re.match(r'^\s*class\s+(\w+)\s*(?:\{|:)', line)
        if m:
            name = m.group(1)
            # Skip Null* helper classes that appear after the main interface
            if not name.startswith('Null'):
                return name
    return None


def parse_pure_virtual_methods(lines: list[str], class_name: str) -> list[MethodInfo]:
    """Parse all pure virtual method declarations from the class body."""
    methods = []
    in_class = False
    brace_depth = 0

    for line in lines:
        stripped = line.strip()

        # Detect class start
        if not in_class:
            if re.match(rf'^\s*class\s+{re.escape(class_name)}\s*(?:\{{|:)', stripped):
                in_class = True
                if '{' in stripped:
                    brace_depth = 1
                continue
            continue

        # Track brace depth
        brace_depth += stripped.count('{') - stripped.count('}')
        if brace_depth <= 0:
            break

        # Only match pure virtual methods (= 0)
        m = re.match(
            r'^\s*virtual\s+'          # virtual keyword
            r'(.+?)\s+'               # return type (non-greedy)
            r'(\w+)\s*'              # method name
            r'\(([^)]*)\)\s*'         # parameter list
            r'(const\s*)?'            # optional const
            r'=\s*0\s*;',            # pure virtual marker
            stripped
        )
        if m:
            return_type = m.group(1).strip()
            name = m.group(2).strip()
            params = m.group(3).strip()
            const = m.group(4) is not None

            methods.append(MethodInfo(
                return_type=return_type,
                name=name,
                params=params,
                const=const,
                original_line=stripped,
            ))

    return methods


def build_include_path(header_path: str) -> str:
    """
    Convert a filesystem path to the include path used in the codebase.

    Strips everything up to and including 'server/' prefix, yielding paths like:
        L4_drivers/boundary/imotor_driver.hpp
        F_platform/boundary/iclock.hpp
    """
    # Normalize to forward slashes
    normalized = header_path.replace('\\', '/')

    # Find 'server/' prefix and strip it (and parent dirs)
    parts = normalized.split('/')
    for i, part in enumerate(parts):
        if part == 'server':
            remaining = '/'.join(parts[i + 1:])
            # Strip 'layers/' or 'foundation/' intermediate dirs
            remaining = re.sub(r'^layers/', '', remaining)
            remaining = re.sub(r'^foundation/', '', remaining)
            return remaining

    # Fallback: just use the filename portion from boundary/ onward
    m = re.search(r'(\w+/boundary/\w+\.hpp)$', normalized)
    if m:
        return m.group(1)

    return os.path.basename(normalized)


def infer_consumer_include(provider_include: str, consumer_ns: str) -> str:
    """
    Infer the consumer port include path from the provider include and consumer namespace.

    Maps namespace to layer directory:
        Board     -> L5_board
        Drivers   -> L4_drivers
        Services  -> L3_services
        Protocol  -> L2_protocol
        Transport -> L1_transport
        Platform  -> F_platform
        Scheduler -> F_platform/tasks
    """
    ns_to_layer = {
        'Board':     'L5_board',
        'Drivers':   'L4_drivers',
        'Services':  'L3_services',
        'Protocol':  'L2_protocol',
        'Transport': 'L1_transport',
        'Platform':  'F_platform',
        'Scheduler': 'F_platform/tasks',
    }

    layer_dir = ns_to_layer.get(consumer_ns)
    if not layer_dir:
        print(f"Warning: Unknown consumer namespace '{consumer_ns}'. "
              f"Known: {', '.join(ns_to_layer.keys())}",
              file=sys.stderr)
        layer_dir = consumer_ns.lower()

    # Extract just the filename from the provider include
    filename = provider_include.split('/')[-1]

    return f"{layer_dir}/boundary/{filename}"


def format_param_names(params: str) -> str:
    """Extract just the parameter names from a parameter list for forwarding."""
    if not params.strip():
        return ''

    parts = []
    for param in split_params(params):
        param = param.strip()
        if not param:
            continue
        # Remove default values
        param = param.split('=')[0].strip()
        # The last token is the name (handle const, *, &, etc.)
        tokens = param.split()
        name = tokens[-1].lstrip('*&')
        parts.append(name)

    return ', '.join(parts)


def split_params(params: str) -> list[str]:
    """Split parameter list respecting nested angle brackets and parens."""
    result = []
    depth = 0
    current = []

    for ch in params:
        if ch in '<(':
            depth += 1
            current.append(ch)
        elif ch in '>)':
            depth -= 1
            current.append(ch)
        elif ch == ',' and depth == 0:
            result.append(''.join(current))
            current = []
        else:
            current.append(ch)

    if current:
        result.append(''.join(current))

    return result


def generate_adapter(
    provider_ns: str,
    provider_class: str,
    consumer_ns: str,
    adapter_name: str,
    methods: list[MethodInfo],
    provider_include: str,
    consumer_include: str,
) -> str:
    """Generate the complete adapter .hpp file content."""
    # Convert CamelCase to snake_case for the filename
    filename = re.sub(r'(?<!^)(?=[A-Z])', '_', adapter_name).lower()

    lines = []
    lines.append(f'/**')
    lines.append(f' * @file {filename}.hpp')
    lines.append(f' * @brief Harness adapter: '
                 f'{provider_ns}::{provider_class} --> '
                 f'{consumer_ns}::{provider_class}')
    lines.append(f' *')
    lines.append(f' * Auto-generated by generate_adapter.py.')
    lines.append(f' * Trivial forwarder -- both interfaces are structurally identical.')
    lines.append(f' */')
    lines.append(f'')
    lines.append(f'#pragma once')
    lines.append(f'')
    lines.append(f'#include "{provider_include}"')
    lines.append(f'#include "{consumer_include}"')
    lines.append(f'')
    lines.append(f'namespace Harness {{')
    lines.append(f'')
    lines.append(f'class {adapter_name} : public {consumer_ns}::{provider_class} {{')
    lines.append(f'public:')
    lines.append(f'    explicit {adapter_name}({provider_ns}::{provider_class}& provider)'
                 f' : m_provider(provider) {{}}')

    for method in methods:
        lines.append(f'')
        const_suffix = ' const' if method.const else ''
        param_names = format_param_names(method.params)

        # Check if return type is void, primitive, or needs conversion
        rt = method.return_type
        is_void = rt == 'void'
        is_primitive = rt in (
            'bool', 'int', 'unsigned', 'float', 'double',
            'uint8_t', 'uint16_t', 'uint32_t', 'uint64_t',
            'int8_t', 'int16_t', 'int32_t', 'int64_t',
            'size_t', 'char',
        )
        is_pointer = '*' in rt
        is_reference = '&' in rt and '*' not in rt

        if is_void:
            lines.append(f'    void {method.name}({method.params})'
                         f'{const_suffix} override {{')
            lines.append(f'        m_provider.{method.name}({param_names});')
            lines.append(f'    }}')
        elif is_primitive or is_pointer or is_reference:
            lines.append(f'    {rt} {method.name}({method.params})'
                         f'{const_suffix} override {{')
            lines.append(f'        return m_provider.{method.name}({param_names});')
            lines.append(f'    }}')
        else:
            # Non-primitive return type -- might need namespace conversion.
            # Check if the return type is qualified with the provider namespace.
            if rt.startswith(provider_ns + '::'):
                # Provider-qualified type: need to convert to consumer type.
                bare_type = rt[len(provider_ns) + 2:]
                consumer_type = f'{consumer_ns}::{bare_type}'
                provider_type = rt

                lines.append(f'    {consumer_type} {method.name}({method.params})'
                             f'{const_suffix} override {{')
                lines.append(f'        {provider_type} result = '
                             f'm_provider.{method.name}({param_names});')
                lines.append(f'        // TODO: verify conversion -- '
                             f'use {{result.raw}} for structs, '
                             f'static_cast for enums')
                lines.append(f'        return {consumer_type}{{result.raw}};')
                lines.append(f'    }}')
            else:
                # Unqualified type -- lives in provider namespace implicitly.
                # The consumer version should have the same name.
                consumer_type = f'{consumer_ns}::{rt}'
                provider_type = f'{provider_ns}::{rt}'

                lines.append(f'    {consumer_type} {method.name}({method.params})'
                             f'{const_suffix} override {{')
                lines.append(f'        {provider_type} result = '
                             f'm_provider.{method.name}({param_names});')
                lines.append(f'        // TODO: verify conversion -- '
                             f'use {{result.raw}} for structs, '
                             f'static_cast for enums')
                lines.append(f'        return {consumer_type}{{result.raw}};')
                lines.append(f'    }}')

    lines.append(f'')
    lines.append(f'private:')
    lines.append(f'    {provider_ns}::{provider_class}& m_provider;')
    lines.append(f'}};')
    lines.append(f'')
    lines.append(f'}} // namespace Harness')
    lines.append(f'')

    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser(
        description='Generate a trivial forwarding adapter for Ports & Adapters architecture.',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python scripts/generate_adapter.py \\
      server/layers/L4_drivers/boundary/imotor_driver.hpp \\
      MotorDriverAdapter Services

  python scripts/generate_adapter.py \\
      server/layers/L5_board/boundary/igpio.hpp \\
      GPIOAdapter Drivers --output server/harness/adapters/gpio_adapter.hpp
        """,
    )
    parser.add_argument('provider_header',
                        help='Path to the provider interface header file')
    parser.add_argument('adapter_name',
                        help='Name for the generated adapter class')
    parser.add_argument('consumer_ns',
                        help='Namespace of the consumer port '
                             '(e.g. Services, Drivers, Protocol)')
    parser.add_argument('--output', '-o',
                        help='Output file path (default: stdout)')

    args = parser.parse_args()

    # Read the provider header
    header_path = args.provider_header
    if not os.path.isfile(header_path):
        print(f"Error: Provider header not found: {header_path}", file=sys.stderr)
        sys.exit(1)

    with open(header_path, 'r', encoding='utf-8') as f:
        content = f.read()
    lines = content.splitlines()

    # Parse the header
    provider_ns = find_namespace(lines)
    if not provider_ns:
        print("Error: Could not find namespace in provider header.", file=sys.stderr)
        sys.exit(1)

    class_name = find_class_name(lines)
    if not class_name:
        print("Error: Could not find interface class in provider header.", file=sys.stderr)
        sys.exit(1)

    methods = parse_pure_virtual_methods(lines, class_name)
    if not methods:
        print(f"Warning: No pure virtual methods found in {class_name}.",
              file=sys.stderr)

    # Build include paths
    provider_include = build_include_path(header_path)
    consumer_include = infer_consumer_include(provider_include, args.consumer_ns)

    # Inform the user
    print(f"Provider:  {provider_ns}::{class_name}", file=sys.stderr)
    print(f"Consumer:  {args.consumer_ns}::{class_name}", file=sys.stderr)
    print(f"Adapter:   Harness::{args.adapter_name}", file=sys.stderr)
    print(f"Methods:   {len(methods)}", file=sys.stderr)
    print(f"Provider include: {provider_include}", file=sys.stderr)
    print(f"Consumer include: {consumer_include}", file=sys.stderr)

    # Generate the adapter
    output = generate_adapter(
        provider_ns=provider_ns,
        provider_class=class_name,
        consumer_ns=args.consumer_ns,
        adapter_name=args.adapter_name,
        methods=methods,
        provider_include=provider_include,
        consumer_include=consumer_include,
    )

    if args.output:
        output_path = args.output
        os.makedirs(os.path.dirname(output_path) or '.', exist_ok=True)
        with open(output_path, 'w', encoding='utf-8', newline='\n') as f:
            f.write(output)
        print(f"\nWrote: {output_path}", file=sys.stderr)
    else:
        print(f"\n--- Generated adapter ---\n", file=sys.stderr)
        print(output)


if __name__ == '__main__':
    main()
