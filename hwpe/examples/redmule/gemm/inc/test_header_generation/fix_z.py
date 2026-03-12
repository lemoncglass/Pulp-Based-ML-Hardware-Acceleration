"""
    Thank you Claude :)
"""

import numpy as np

def read_c_array(filename):
    with open(filename, 'r') as f:
        content = f.read()
    
    # Extract only the active definition
    import re
    match = re.search(r'#define\s+[A-Z]\s+\{\s*\\?([^}]+)\}', content)
    if not match:
        return []
    
    array_str = match.group(1)
    vals = [int(v, 16) for v in re.findall(r'0x[0-9a-fA-F]+', array_str)]
    return vals

x_vals = read_c_array('./x_input.h')
w_vals = read_c_array('./w_input.h')

print(f"X length: {len(x_vals)}, W length: {len(w_vals)}")

if len(x_vals) == 3072 and len(w_vals) == 3072:
    X = np.array(x_vals).reshape(48, 64)
    W = np.array(w_vals).reshape(64, 48)
    Z = X @ W
    
    print(f"Z shape: {Z.shape}, first val: {hex(Z[0,0])}")
    
    with open('./z_output.h', 'w') as f:
        f.write("#define Z { \\\n")
        z_flat = Z.flatten()
        for i, val in enumerate(z_flat):
            f.write(f"0x{val:x}, ")
            if (i+1) % 16 == 0:
                f.write(" \\\n")
        f.write("}\n")
