import os
import glob
import gzip

# Gzips a file in memory then writes it as a C++ PROGMEM byte array header: const PROGMEM char <name>gz[] = {...};
def convert_file_to_cppheader(filename):
    with open(filename,'rb') as webfile:
        gz_bytes=gzip.compress(webfile.read(),compresslevel=9)
    varname=os.path.basename(filename).replace(' ','').replace('.','').replace('-','')
    with open(filename+'.gz.h','w') as hfile:
        hfile.write('const PROGMEM char '+varname+'gz[] = {')
        hfile.write(','.join(hex(b) for b in gz_bytes))
        hfile.write('};')

# Returns True if the .gz.h header is missing or its decompressed content differs from the source file
# The header is decoded from hex back to bytes and decompressed in memory
def is_convert_needed(filename):
    # check if gz.h file exists
    hfilename=filename+'.gz.h'
    if not os.path.exists(hfilename):
        return True
    # convert h file to gz file
    with open(hfilename) as hfile:
        # read the one line header file
        line=hfile.readline()
        # keep content between '{' and '}' and split hex values by ','
        hexvalues=line[line.find('{')+1:line.find('}')].split(',')
    # read file inside gz in memory
    decompressed=gzip.decompress(bytes(int(h,16) for h in hexvalues))
    # read web file
    with open(filename,'rb') as webfile:
        # return True if gz file content and web file are different
        return decompressed != webfile.read()

#Convert all Web Files in a folder
def convert_webfiles(pattern):
    for file in glob.glob(pattern):
        if is_convert_needed(file):
            print('Converting %s to header' % file)
            convert_file_to_cppheader(file)

# Writes a file as a raw (uncompressed) C++ PROGMEM byte array header: const PROGMEM char <name>[] = {...};
# Used for files that are already compressed (e.g. woff2), where gzipping again wastes CPU with no size benefit.
def convert_file_to_raw_cppheader(filename):
    with open(filename,'rb') as webfile:
        raw_bytes=webfile.read()
    varname=os.path.basename(filename).replace(' ','').replace('.','').replace('-','')
    with open(filename+'.gz.h','w') as hfile:
        hfile.write('const PROGMEM char '+varname+'[] = {')
        hfile.write(','.join(hex(b) for b in raw_bytes))
        hfile.write('};')

# Returns True if the header is missing or its content differs from the source file
def is_convert_needed_raw(filename):
    hfilename=filename+'.gz.h'
    if not os.path.exists(hfilename):
        return True
    with open(hfilename) as hfile:
        line=hfile.readline()
        hexvalues=line[line.find('{')+1:line.find('}')].split(',')
    stored=bytes(int(h,16) for h in hexvalues)
    with open(filename,'rb') as webfile:
        return stored != webfile.read()

#Convert all binary Web Files (e.g. fonts) in a folder without gzipping them
def convert_binary_webfiles(pattern):
    for file in glob.glob(pattern):
        if is_convert_needed_raw(file):
            print('Converting %s to header (raw)' % file)
            convert_file_to_raw_cppheader(file)

print('--- pio_pre_webfiles_to_headers.py start ---')

convert_webfiles('./src/base/data/*.js')
convert_webfiles('./src/base/data/*.css')
convert_webfiles('./src/base/data/index.html')
convert_binary_webfiles('./src/base/data/fonts/*.woff2')

print('--- pio_pre_webfiles_to_headers.py end ---')
print()