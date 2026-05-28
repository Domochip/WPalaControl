import sys
import os
import glob
import gzip
import shutil

#Convert one file to header
#file is first GZipped then converted to header file (hex in PROGMEM)
def convert_file_to_cppheader(filename):
    with open(filename,'rb') as webfile:
        with gzip.open(filename+'.gz','wb',9) as intogzipfile:
            shutil.copyfileobj(webfile,intogzipfile)
    with open(filename+'.gz','rb') as gzfile:
        with open(filename+'.gz.h','w') as hfile:
            varname = os.path.basename(filename).replace(' ','').replace('.','').replace('-','')
            hfile.write('const PROGMEM char '+varname+'gz[] = {')
            byte=gzfile.read(1)
            first=True
            while len(byte):
                if not first:
                    hfile.write(',')
                if sys.version_info.major == 2:
                    hfile.write(hex(ord(byte)))
                else:
                    hfile.write(hex(byte[0]))
                first=False
                byte=gzfile.read(1)
            hfile.write('};')
    os.remove(filename+'.gz')

#Check if file needs to be converted
#header is converted back to gz file and file inside is compared to web file
def is_convert_needed(filename):
    # check if gz.h file exists
    hfilename=filename+'.gz.h'
    if not os.path.exists(hfilename):
        return True
    # convert h file to gz file
    with open(hfilename) as hfile:
        with open(filename+'.gz','wb') as gzfile:
            # read the one line header file
            line=hfile.readline()
            # keep content between '{' and '}'
            hexvalues=line[line.find('{')+1:line.find('}')]
            # split hex values by ','
            hexvalues=hexvalues.split(',')
            # convert hex values to bytes
            bytes=bytearray()
            for hexvalue in hexvalues:
                bytes.append(int(hexvalue,16))
            # write bytes to gzip file
            gzfile.write(bytes)
    # read file inside gz
    with gzip.open(filename+'.gz','rb') as intogzipfile:
        intogzipfilecontent=intogzipfile.read()
    # remove gz file
    os.remove(filename+'.gz')
    # read web file
    with open(filename,'rb') as webfile:
        webfilecontent=webfile.read()
    # return True if gz file and web file are different
    return (intogzipfilecontent != webfilecontent)

#Convert all Web Files in a folder
def convert_webfiles(pattern):
    for file in glob.glob(pattern):
        if is_convert_needed(file):
            print('Converting %s to header' % file)
            convert_file_to_cppheader(file)

print('--- pio_pre_webfiles_to_headers.py start ---')

convert_webfiles('./src/base/data/*.js')
convert_webfiles('./src/base/data/*.css')
convert_webfiles('./src/base/data/index.html')

print('--- pio_pre_webfiles_to_headers.py end ---')
print()