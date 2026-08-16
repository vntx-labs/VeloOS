import struct
import os

def build_fs():
    # 1. os.bin öffnen
    if not os.path.exists("os.bin"):
        print("Fehler: os.bin existiert nicht!")
        return
        
    with open("os.bin", "r+b") as f:
        # Sicherstellen, dass das Image groß genug ist (mindestens 50 Sektoren)
        f.truncate(512 * 50)
        
        # 2. matrix.bin einlesen
        if not os.path.exists("matrix.bin"):
            print("Fehler: matrix.bin fehlt! Baue erst die Matrix.")
            return
            
        with open("matrix.bin", "rb") as mf:
            matrix_data = mf.read()
            
        matrix_size = len(matrix_data)
        matrix_start_sector = 41
        
        # 3. Inode für Sektor 40 bauen (32 Bytes Name + 4 Bytes StartSektor + 4 Bytes Size)
        # Format-String '32sII' = 32 Bytes char[], 1x uint32_t, 1x uint32_t
        filename = b"matrix"
        inode = struct.pack("<32sII", filename, matrix_start_sector, matrix_size)
        
        # Sektor 40 mit Nullen initialisieren und Inode an den Anfang schreiben
        sector_40_data = inode + b"\x00" * (512 - len(inode))
        
        # 4. In die os.bin schreiben
        # Sektor 40 beginnt bei Byte-Offset 40 * 512 = 20480
        f.seek(40 * 512)
        f.write(sector_40_data)
        
        # Sektor 41 beginnt bei Byte-Offset 41 * 512 = 20992
        f.seek(41 * 512)
        f.write(matrix_data)
        
        print(f"VFS erfolgreich erstellt! matrix (Größe: {matrix_size} Bytes) an Sektor 41 geschrieben.")

if __name__ == "__main__":
    build_fs()
