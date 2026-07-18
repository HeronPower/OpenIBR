#!/usr/bin/env python3
"""
Generic script to generate header files from YAML schema files
"""

import yaml
import sys
import os
from pathlib import Path
from typing import Dict, List, Tuple, Any, Optional


class TypeConverter:
    """Handles type conversion from YAML to C types."""
    
    TYPE_MAPPING = {
        'float': 'float',
        'bool': 'bool',
        'int': 'int',
        'uint': 'unsigned int',
        'cmplx_t': 'cmplx_t',
    }
    
    @classmethod
    def get_c_type(cls, yaml_type: str) -> str:
        """Convert YAML type to C type."""
        return cls.TYPE_MAPPING.get(yaml_type, 'float')
    
    @classmethod
    def get_default_value(cls, value_dict: Dict[str, Any]) -> str:
        """Get the default value from a parameter definition.
        
        Returns string representing the initializer in C.
        Preserves '0.0f' string as-is.
        """
        default_val = value_dict.get('default', 0.0)

        # For bools, lowercase string 'true'/'false'
        if isinstance(default_val, bool):
            return str(default_val).lower()

        # If default_val is string and ends with 'f', keep as is (like '0.0f')
        if isinstance(default_val, str) and default_val.endswith('f'):
            return default_val

        # If default_val is a float or int, convert to string with 'f' suffix for floats
        if isinstance(default_val, float):
            s = f"{default_val:.8g}"
            if '.' not in s:
                s += ".0"
            return s + "f"
        if isinstance(default_val, int):
            return str(default_val)

        # If default_val is a list or nested list, return as is (to be handled later)
        return default_val
    
    @classmethod
    def format_cmplx_array(cls, array_2d: List[List[float]]) -> str:
        """Format a 2D array of floats/strings into C initializer for cmplx_t array."""
        formatted_items = []
        for pair in array_2d:
            r, i = pair[0], pair[1]
            # Format each element as string preserving 0.0f or float with f
            r_str = cls._format_float(r)
            i_str = cls._format_float(i)
            formatted_items.append(f"{{{r_str}, {i_str}}}")
        return "{" + ", ".join(formatted_items) + "}"
    
    @classmethod
    def _format_float(cls, value: Any) -> str:
        """Helper method to format float values consistently."""
        if isinstance(value, float):
            s = f"{value:.8g}"
            if '.' not in s:
                s += ".0"
            return s + "f"
        return str(value)


class SchemaParser:
    """Handles YAML schema parsing and validation."""
    
    def __init__(self, schema_path: str):
        self.schema_path = schema_path
        self.schema_data = self._load_schema()
    
    def _load_schema(self) -> Dict[str, Any]:
        """Load and parse the YAML schema file."""
        with open(self.schema_path, 'r') as file:
            return yaml.safe_load(file)
    
    def get_module_name(self) -> str:
        """Extract module name from metadata or filename."""
        module_name = self.schema_data.get('metadata', {}).get('name', 'unknown')
        
        if module_name == 'unknown':
            # Fallback: try to extract from filename
            schema_filename = os.path.basename(self.schema_path)
            module_name = schema_filename.split('_')[0] if '_' in schema_filename else schema_filename.split('.')[0]
        
        return module_name
    
    def get_filename(self) -> str:
        """Generate filename from module name and type."""
        module_name = self.schema_data.get('metadata', {}).get('name', 'unknown')
        module_type = self.schema_data.get('metadata', {}).get('type', '')
        
        if module_name == 'unknown':
            # Fallback: try to extract from filename
            schema_filename = os.path.basename(self.schema_path)
            module_name = schema_filename.split('_')[0] if '_' in schema_filename else schema_filename.split('.')[0]
        
        # Include type in filename if present, preserving capitalization
        if module_type:
            filename = f"{module_name}_{module_type}"
        else:
            filename = module_name
            
        return filename
    
    def get_groups(self) -> Dict[str, Any]:
        """Get all groups except metadata."""
        return {k: v for k, v in self.schema_data.items() if k != 'metadata'}
    
    def get_config_groups(self) -> Dict[str, Any]:
        """Get only config groups."""
        groups = self.get_groups()
        return {k: v for k, v in groups.items() 
                if isinstance(v, dict) and v.get('use') == 'config_group'}


class StructGenerator:
    """Handles struct generation for groups."""
    
    @classmethod
    def generate_struct_content(cls, signals: Dict[str, Any], output_type: str = 'members') -> List[str]:
        """Generate struct members or instances for a specific group's signals.
        
        Args:
            signals: Dictionary of signal definitions
            output_type: Either 'members' for struct member declarations or 'instances' for initializations
        """
        content = []
        
        for key, value in signals.items():
            if isinstance(value, dict) and 'type' in value:
                c_type = TypeConverter.get_c_type(value['type'])
                array_size = value.get('array')
                
                if output_type == 'members':
                    content.append(cls._generate_member_declaration(key, c_type, array_size))
                elif output_type == 'instances':
                    content.append(cls._generate_instance_initialization(key, value, c_type, array_size))
        
        return content
    
    @classmethod
    def _generate_member_declaration(cls, key: str, c_type: str, array_size: Optional[int]) -> str:
        """Generate struct member declaration."""
        if array_size:
            return f"    {c_type} {key}[{array_size}];"
        else:
            return f"    {c_type} {key};"
    
    @classmethod
    def _generate_instance_initialization(cls, key: str, value: Dict[str, Any], c_type: str, array_size: Optional[int]) -> str:
        """Generate struct instance initialization."""
        default_val = TypeConverter.get_default_value(value)
        
        if array_size:
            return cls._generate_array_initialization(key, c_type, default_val, array_size)
        else:
            return cls._generate_scalar_initialization(key, default_val)
    
    @classmethod
    def _generate_array_initialization(cls, key: str, c_type: str, default_val: Any, array_size: int) -> str:
        """Generate array initialization."""
        if c_type == 'cmplx_t':
            if isinstance(default_val, list) and all(isinstance(x, list) and len(x) == 2 for x in default_val):
                val_str = TypeConverter.format_cmplx_array(default_val)
                # Ensure we have array_size elements; pad with {0.0f, 0.0f} if needed
                if len(default_val) < array_size:
                    padding = ", ".join(["{0.0f, 0.0f}"] * (array_size - len(default_val)))
                    val_str = val_str[:-1] + ", " + padding + "}"
                return f"    .{key} = {val_str},"
            else:
                # fallback to zero initialize
                return f"    .{key} = {{{', '.join(['{0.0f, 0.0f}']*array_size)}}},"
        else:
            # For float arrays or other scalar arrays
            if isinstance(default_val, list):
                vals = []
                for item in default_val:
                    if isinstance(item, str) and item.endswith('f'):
                        vals.append(item)
                    elif isinstance(item, float):
                        s = f"{item:.8g}"
                        if '.' not in s:
                            s += ".0"
                        vals.append(s + "f")
                    elif isinstance(item, bool):
                        vals.append('true' if item else 'false')
                    else:
                        vals.append(str(item))
                # Pad if needed
                if len(vals) < array_size:
                    vals += ["0.0f"] * (array_size - len(vals))
                val_str = "{" + ", ".join(vals) + "}"
                return f"    .{key} = {val_str},"
            else:
                return f"    .{key} = {{0}},"
    
    @classmethod
    def _generate_scalar_initialization(cls, key: str, default_val: Any) -> str:
        """Generate scalar initialization."""
        if isinstance(default_val, str):
            return f"    .{key} = {default_val},"
        elif isinstance(default_val, float):
            s = f"{default_val:.8g}"
            if '.' not in s:
                s += ".0"
            return f"    .{key} = {s}f,"
        elif isinstance(default_val, list) and len(default_val) == 2:
            # Handle complex number initialization [real, imag] -> {real, imag}
            real_str = TypeConverter._format_float(default_val[0])
            imag_str = TypeConverter._format_float(default_val[1])
            return f"    .{key} = {{{real_str}, {imag_str}}},"
        else:
            return f"    .{key} = {default_val},"


class HeaderGenerator:
    """Handles header file generation."""
    
    def __init__(self, schema_parser: SchemaParser):
        self.schema_parser = schema_parser
        self.schema_data = schema_parser.schema_data
        self.module_name = schema_parser.get_module_name()
        self.filename = schema_parser.get_filename()
    
    def generate_public_header(self) -> str:
        """Generate the public header file with declarations."""
        # Extract metadata
        description = self.schema_data.get('metadata', {}).get('description', f'{self.module_name} component')
        
        # Generate setter functions for config
        config_setters = self._generate_setter_functions()
        
        # Generate struct definitions for each group
        group_structs, extern_declarations = self._generate_group_structs()
        
        # Build the header file content
        header_content = f"""#ifndef {self.module_name}_H
#define {self.module_name}_H

#include "../../helper_lib/common_types.h"

// {description}
// Function declarations
void {self.module_name}_init(void);
void {self.module_name}_isr(float t_step);
void {self.module_name}_1kHz(float t_step);
void {self.module_name}_5kHz(float t_step);
void {self.module_name}_update_derived_config(void);

// Struct definitions for each group
"""

        # Add group structs
        for struct_def in group_structs:
            header_content += f"{struct_def}\n"

        header_content += f"""
// External declarations of the actual instances
"""

        # Add external declarations
        for extern_decl in extern_declarations:
            header_content += f"{extern_decl}\n"

        header_content += f"""
// Setter functions for config parameters
"""

        # Add setter functions
        for setter in config_setters:
            header_content += f"{setter}\n"

        header_content += f"""
#endif // {self.module_name}_H
"""
        
        return header_content
    
    def generate_private_header(self) -> str:
        """Generate the private header file with actual instances."""
        # Extract metadata
        description = self.schema_data.get('metadata', {}).get('description', f'{self.module_name} component')
        
        # Build the private header file content
        private_content = f"""#ifndef {self.module_name}_PRIVATE_H
#define {self.module_name}_PRIVATE_H

#include "{self.filename}_autogen.h"

// {description} - Private implementation
// Actual instances of the structs with default values

"""
        
        # Generate instances for each group
        for group_name, group_data in self.schema_parser.get_groups().items():
            if isinstance(group_data, dict) and 'signals' in group_data:
                signals = group_data.get('signals', {})
                if signals:  # Only create instance if there are signals
                    instances = StructGenerator.generate_struct_content(signals, 'instances')
                    
                    private_content += f"struct {self.module_name}_{group_name} {self.module_name}_{group_name} = {{\n"
                    
                    # Add instances
                    for instance in instances:
                        private_content += f"{instance}\n"
                    
                    private_content += f"}};\n\n"
        
        private_content += f"""#endif // {self.module_name}_PRIVATE_H
"""
        
        return private_content
    
    def _generate_setter_functions(self) -> List[str]:
        """Generate static inline setter function implementations for config parameters."""
        functions = []

        # Iterate through all groups in the schema
        for group_name, group_data in self.schema_parser.get_config_groups().items():
            signals = group_data.get('signals', {})
            for key, value in signals.items():
                if isinstance(value, dict) and 'type' in value:
                    c_type = TypeConverter.get_c_type(value['type'])
                    function_name = f"set_{self.module_name}_{group_name}_{key}"
                    functions.append(
                        f"static inline void {function_name}({c_type} value) "
                        f"{{ {self.module_name}_{group_name}.{key} = value; }}"
                    )

        return functions
    
    def _generate_group_structs(self) -> Tuple[List[str], List[str]]:
        """Generate struct definitions for each group."""
        structs = []
        extern_declarations = []
        
        # Iterate through all groups in the schema
        for group_name, group_data in self.schema_parser.get_groups().items():
            if isinstance(group_data, dict) and 'signals' in group_data:
                signals = group_data.get('signals', {})
                if signals:  # Only create struct if there are signals
                    # Generate struct members
                    members = StructGenerator.generate_struct_content(signals, 'members')
                    
                    # Create struct definition
                    struct_def = f"""
// {group_data.get('description', f'{group_name} group')}
struct {self.module_name}_{group_name} {{
"""
                    for member in members:
                        struct_def += f"{member}\n"
                    
                    struct_def += f"}};"
                    structs.append(struct_def)
                    
                    # Add external declaration
                    extern_declarations.append(f"extern struct {self.module_name}_{group_name} {self.module_name}_{group_name};")
        
        return structs, extern_declarations


def main():
    """Main function to generate the header file."""
    if len(sys.argv) < 3:
        print("Usage: python generate_header.py <schema_file> <target_folder>")
        print("Example: python generate_header.py ac_checks_A_schema.yaml source/ac_checks")
        sys.exit(1)
    
    schema_path = sys.argv[1]
    target_folder = sys.argv[2]
    
    # Check if schema file exists
    if not os.path.exists(schema_path):
        print(f"Error: Schema file '{schema_path}' not found.")
        sys.exit(1)
    
    try:
        # Parse schema
        schema_parser = SchemaParser(schema_path)
        
        # Generate headers
        header_generator = HeaderGenerator(schema_parser)
        public_header_content = header_generator.generate_public_header()
        private_header_content = header_generator.generate_private_header()
        
        # Create target directory if it doesn't exist
        output_dir = Path(target_folder)
        output_dir.mkdir(parents=True, exist_ok=True)
        
        # Write the header files
        public_output_path = output_dir / f"{schema_parser.get_filename()}_autogen.h"
        private_output_path = output_dir / f"{schema_parser.get_filename()}_private_autogen.h"
        
        with open(public_output_path, 'w') as file:
            file.write(public_header_content)
        
        with open(private_output_path, 'w') as file:
            file.write(private_header_content)
        
        print(f"Successfully generated {public_output_path}")
        print(f"Successfully generated {private_output_path}")
        
    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)

if __name__ == "__main__":
    main() 