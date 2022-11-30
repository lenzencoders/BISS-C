'''
 _     _____ _   _ _____   _____ _   _  ____ ___  ____  _____ ____  ____  
| |   | ____| \ | |__  /  | ____| \ | |/ ___/ _ \|  _ \| ____|  _ \/ ___| 
| |   |  _| |  \| | / /   |  _| |  \| | |  | | | | | | |  _| | |_) \___ \ 
| |___| |___| |\  |/ /_   | |___| |\  | |__| |_| | |_| | |___|  _ < ___) |
|_____|_____|_| \_/____|  |_____|_| \_|\____\___/|____/|_____|_| \_|____/ 
                                                                          
'''
import numpy as np
import pandas as pd
import binascii
import signal
from time import sleep
import sys
import lenz_gu_lib as gl
import logging
import argparse
from hashlib import md5

logfilename = 'lenz_calibration_encrypt.log'
logger = logging.getLogger()
logger.setLevel(logging.DEBUG)
stdout_handler = logging.StreamHandler(sys.stdout)
stdout_handler.setLevel(logging.DEBUG)
stdout_formatter = logging.Formatter("%(message)s")
stdout_handler.setFormatter(stdout_formatter)
file_handler = logging.FileHandler(logfilename)
file_formatter = logging.Formatter("%(asctime)s %(processName)s %(name)s [%(funcName)s] %(levelname)s %(message)s")
file_handler.setFormatter(file_formatter)
file_handler.setLevel(logging.DEBUG)
logger.addHandler(file_handler)
logger.addHandler(stdout_handler)
gl.initLogger(logger)

version = 'v0.5'


def calibration_encrypt(input_file, output_file, ciph_file,
                        ctable_file, dry_run):

    def signal_handler(signum, frame):
        print('', end="\n", flush=True)
        logging.info('Ctrl-c was pressed. Wrapping up...')
        try:
            gu.close()
        except Exception:
            logging.debug('LENZ Board wasn\'t initialized...')
        sys.exit(1)

    def Dif2enchex(DifTable, filename, ciph):
        logging.info('Encrypting calibration data with ciph')
        l_dif = len(DifTable)
        if l_dif < 64:
            DifTable = np.append(DifTable, [0] * (64 - l_dif))
            l_dif = 64
        dif_re = np.byte(DifTable)
        data = [2, 0, 0, 4, 12, (l_dif >> 6)-1]
        data.append(256 - sum(data) % 256)
        temp_hex = [gl.dec2hex(x) for x in data]
        hexdataout = []
        hexdataout.append(":" + "".join(temp_hex))
        data_count = 0
        bn = 384
        for j in range(1, (l_dif >> 6) + 1):
            dif_bytes = bytes(dif_re[data_count:data_count + 64])
            temp_enc = bytes(a ^ b for a, b in zip(dif_bytes, ciph[j-1]))
            temp_hex = gl.dec2hex(64) + gl.dec2hex(bn >> 8) + gl.dec2hex(bn % 256) + '00' + temp_enc.hex()
            CRC = int(temp_hex[0:2], 16)
            for i in range(2, len(temp_hex), 2):
                CRC = CRC + int(temp_hex[i:i+2], 16)
            temp_hex = temp_hex + gl.dec2hex(int(-CRC % 256))
            hexdataout.append(":" + "".join(temp_hex))
            data_count += 64
            bn += 1
        temp_hex = '04000003' + gl.dec2hex(binascii.crc32(bytes(dif_re)))
        CRC = int(temp_hex[0:2], 16)
        for i in range(2, len(temp_hex), 2):
            CRC = CRC + int(temp_hex[i:i+2], 16)
        temp_hex = temp_hex + gl.dec2hex(int(256 - CRC % 256))
        hexdataout.append(":" + "".join(temp_hex))
        hexdataout.append(":00000001FF")
        with open(filename, "w") as output:
            for row in hexdataout:
                output.write(str(row).upper() + '\n')
        logging.info('Encrypting done.')

    def ciphfromfile(filename):
        read_bytes = []
        count = 2048
        with open(filename, "rb") as f:
            byte = f.read(1)
            while byte:
                read_bytes.append(byte)
                byte = f.read(1)
        bytesdata = []
        for i in range((count >> 6)):
            bytesrow = []
            for j in range(i << 6, (i + 1) << 6):
                bytesrow.append(read_bytes[j])
            bytesdata.append(b''.join(bytesrow))
        logging.info('Cipher MD5 hash: ' + md5(b''.join(bytesdata)).hexdigest())
        return bytesdata

    def sumdiftables(dif1, dif2):
        DifTable = np.int16(dif1 + dif2)
        for i in range(len(DifTable)):
            if DifTable[i] > 127:
                DifTable[i] = 127
            if DifTable[i] < -128:
                DifTable[i] = -128
        return DifTable

    logging.debug('Upload initiated.')
    logging.debug(f'Data file: {input_file}')
    logging.debug(f'Cipher file: {ciph_file}') 
    logging.debug(f'Calibration data: {ctable_file}') 
    logging.debug(f'Dry-run: {dry_run}')

    fullcal_data = pd.read_csv(input_file, header=None)  # lib_FullCal_diftable.csv
    if ctable_file:
        ctable = pd.read_csv(ctable_file, header=None)
        if len(ctable) > 2048 or len(fullcal_data) > 2048:
            logging.error(f'{ctable_file} {len(ctable)} values; {input_file}: {len(fullcal_data)} values')
            sys.exit('Provided calibration data exceeds 2048 values.')
        SummedTable = sumdiftables(fullcal_data, ctable)

    ciph12 = ciphfromfile(ciph_file)
    DifTable = SummedTable if ctable_file else fullcal_data
    Dif2enchex(DifTable, output_file, ciph12)

    dry_run and sys.exit('Dry run, did not recalibrate the encoder')

    logging.info('Going to recalibrate the Encoder!!! You have 3 seconds to STOP.')
    sleep(5)

    signal.signal(signal.SIGINT, signal_handler)

    gu = gl.GU()

    gu.load_enc_hex(output_file)
    try:
        gu.biss_registers()
    except IndexError:
        pass
    gu.close()
    logging.info('All done!')


def data_reading():

    def signal_handler(signum, frame):
        print('', end="\n", flush=True)
        logging.info('Ctrl-c was pressed. Wrapping up...')
        try:
            gu.close()
        except Exception:
            logging.debug('LENZ Board wasn\'t initialized...')
        sys.exit(1)

    def std(ans2, degrs, degree_sign, mins, secs):
        sys.stdout.write("\r" + f'[{ans2}]: {str(degrs):>3}{degree_sign} {str(mins):2}\' {str(secs):2}\"' + '\t\t')
        sys.stdout.flush()

    def printflow(ans2, degrs, degree_sign, mins, secs):
        logging.debug(f'[{ans2}]: {str(degrs):>3}{degree_sign} {str(mins):2}\' {str(secs):2}\"')

    gl.initLogger(logger)
    signal.signal(signal.SIGINT, signal_handler)

    gu = gl.GU()
    try:
        gu.biss_registers()
    except IndexError:
        pass
    print()
    degree_sign = u"\N{DEGREE SIGN}"

    outcmd = printflow if flow else std
    logging.info('Starting infinite loop for angle reading. Press STOP to kill the loop.')
    while True:
        sleep(0.1)
        ans = gu.biss_read_data()
        ans2 = ans[0:2] + ' ' + ans[2:4] + ' ' + ans[4:6] + ' ' + ans[6:8] + ' ' \
            + ans[8:10] + ' ' + ans[10:12] + ' ' + ans[12:14] + ' ' + ans[14:16]
        ang = int(ans[8:14], 16) * 360 / 2 ** 24
        degrs = int(ang)
        mins = int((ang - degrs)*60)
        secs = int((ang - degrs - (mins / 60)) * 3600)
        outcmd(ans2, degrs, degree_sign, mins, secs)

    gu.close()  # yeah, i know


desctext = "Uploading calibration data to Lenz Encoders. (c) LENZ ENCODERS (https://lenzencoders.com), 2020-2022 \n\n "
parser = argparse.ArgumentParser(description=desctext)
subparser = parser.add_subparsers(dest='command')
upload = subparser.add_parser('upload')
connect = subparser.add_parser('connect')
parser.add_argument("-v", "--version",  help="Show the program version", action="store_true")
upload.add_argument("-i", "--input_file", help="Diftable in csv file, containing a calibration data you want uploaded",
                    required=True)
upload.add_argument("-t", "--original_ctable", help="Calibration table in csv provided by Lenz Encoders")
upload.add_argument("-o", "--output_file", help="Output to save encrypted hex file", required=True)
upload.add_argument("-c", "--ciph_file", help="The cipher file provided by Lenz Encoders", required=True)
upload.add_argument("-d", "--dry-run", help="Do a dry run without having to actually make changes to the encoder",
                    action="store_true")
connect.add_argument("-f", "--flow", help="Angle output in continuous loop with separeted string for each reading",
                     action="store_true")
args = parser.parse_args()
args.version and print(f"LENZ ENCODERS/Uploader {version}")

if args.command == 'upload':
    logging.info(f'{"#"*10} Program starting in upload mode {"#"*10}')
    input_file = args.input_file if args.input_file else False
    output_file = args.output_file if args.output_file else False
    ciph_file = args.ciph_file if args.ciph_file else False
    ctable_file = args.original_ctable if args.original_ctable else False
    dry_run = True if args.dry_run else False
    calibration_encrypt(input_file=input_file, output_file=output_file, ciph_file=ciph_file,
                        ctable_file=ctable_file, dry_run=dry_run)
elif args.command == 'connect':
    logging.info(f'{"#"*10} Program starting in connection mode {"#"*10}')
    flow = True if args.flow else False
    data_reading()
else:
    print(parser.format_help())
