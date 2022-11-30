'''
 _     _____ _   _ _____   _____ _   _  ____ ___  ____  _____ ____  ____  
| |   | ____| \ | |__  /  | ____| \ | |/ ___/ _ \|  _ \| ____|  _ \/ ___| 
| |   |  _| |  \| | / /   |  _| |  \| | |  | | | | | | |  _| | |_) \___ \ 
| |___| |___| |\  |/ /_   | |___| |\  | |__| |_| | |_| | |___|  _ < ___) |
|_____|_____|_| \_/____|  |_____|_| \_|\____\___/|____/|_____|_| \_|____/ 
                                                                          
'''
import serial
import time
import serial.tools.list_ports
import numpy as np
import pandas as pd
import os
import sys

logging = None


def initLogger(mainlogger=None):
    global logging
    if mainlogger is not None:
        logging = mainlogger
    else:
        import logging
        logfilename = 'lenz_calibration_encrypt.log'
        logger = logging.getLogger()
        logger.setLevel(logging.DEBUG)
        stdout_handler = logging.StreamHandler(sys.stdout)
        stdout_handler.setLevel(logging.INFO)
        stdout_formatter = logging.Formatter("%(message)s")
        stdout_handler.setFormatter(stdout_formatter)
        file_handler = logging.FileHandler(logfilename)
        file_formatter = logging.Formatter("%(asctime)s %(processName)s %(name)s \
                                           [%(funcName)s] %(levelname)s %(message)s")
        file_handler.setFormatter(file_formatter)
        file_handler.setLevel(logging.DEBUG)
        logger.addHandler(file_handler)
        logger.addHandler(stdout_handler)


b_read_data = bytearray([0, 0, 0, 132, 124])
b_poweroff = bytearray([0, 0, 11, 128, 128-11])  # encoder power off
b_poweron = bytearray([0, 0, 12, 128, 128-12])  # encoder power on
b_ssi = bytearray([0, 0, 13, 128, 128-13])  # encoder SSI mode
b_uart = bytearray([0, 0, 14, 128, 128-14])  # encoder UART mode for bootloader

b_start = bytearray([5, 49, 246, 185])
b_ans = bytearray([249, 78, 177, 6])
b_ssimode_ans = bytearray([0, 0, 13, 144, 99])


def dec2hex(nr):
    h = format(int(nr), 'x')
    return '0' + h if len(h) % 2 else h


def readhex(filepath):
    try:
        if not os.path.exists(filepath):
            print("File not found!")
            sys.exit(1)
    except OSError as err:
        print(err.reason)
        sys.exit(1)
    dataframe = pd.read_csv(filepath, sep=":", header=None, usecols=[1])
    dataframe = dataframe.squeeze('columns')
    return dataframe


class GU:
    __instance = None

    def __new__(cls, *args, **kwargs):  # singleton
        if cls.__instance is None:
            cls.__instance = super().__new__(cls)
        return cls.__instance

    def __del__(self):
        GU.__instance = None

    def __init__(self):
        ports = serial.tools.list_ports.comports(include_links=False)
        if os.name == 'nt':
            for porti in ports:
                if porti.description[:15] == 'USB Serial Port' or porti.description[:16] == 'USB-SERIAL CH340':
                    self.__port_name = porti.device
                    try:
                        self.__port = serial.Serial(porti.device, 3000000)
                        self.__port.set_buffer_size(rx_size=16777216, tx_size=16384)
                        logging.info(f'LENZ Board {self.__port_name} - Connected!')
                        self.__uart_mode = True
                        break
                    except serial.SerialException:
                        self.__port_state = 'Used'
                        logging.error(f'LENZ Board: {self.__port_name} is being used! Exiting.')
                        sys.exit(1)
            else:
                self.__port_state = 'Not Found'
                logging.error('Error: LENZ Board - not found!')
                logging.info('Program expectedly closed.')
                sys.exit(1)
        else:
            self.__port_name = '/dev/ttyUSB0'
            if os.path.exists(self.__port_name):
                try:
                    self.__port = serial.Serial(self.__port_name, 3000000)
                    logging.info(f'LENZ Board {self.__port_name} - Connected!')
                    self.__uart_mode = True
                except serial.SerialException:
                    self.__port_state = 'Used'
                    logging.error(f'LENZ Board: {self.__port_name} is being used! Exiting')
                    sys.exit(1)
            else:
                logging.error(f'LENZ Board: {self.__port_name} - not found!')
                logging.info('Program expectedly closed.')
                sys.exit(1)

    def retry(times, exceptions):
        """
        Retry Decorator
        Retries the wrapped function/method `times` times if the exceptions listed
        in ``exceptions`` are thrown
        :param times: The number of times to repeat the wrapped function/method
        :type times: Int
        :param Exceptions: Lists of exceptions that trigger a retry attempt
        :type Exceptions: Tuple of Exceptions
        """
        def decorator(func):
            def newfunc(*args, **kwargs):
                attempt = 0
                while attempt < times:
                    try:
                        return func(*args, **kwargs)
                    except exceptions:
                        print('Exception thrown when attempting to run %s, attempt %d of %d' % (func, attempt, times))
                        attempt += 1
                return func(*args, **kwargs)
            return newfunc
        return decorator

    def start(self):
        self.__port.baudrate = 460800

    def power_off(self):
        self.__port.write(b_poweroff)  # Encoder Power off
        logging.debug('Turning off encoder')
        time.sleep(0.001)

    def power_on(self):
        self.__port.write(b_poweron)  # Encoder Power on
        logging.debug('Rebooting encoder')
        time.sleep(0.001)

    def close(self):
        self.__port.close()
        logging.debug(f'LENZ Board: {self.__port_name} - disconnected!')

    def uart_mode(self):
        self.__port.write(b_uart)  # Encoder UART mode
        logging.debug('Turning on encoder UART mode')
        self.__uart_mode = True
        time.sleep(0.001)

    def ssi_mode(self):
        self.__port.flushInput()
        self.__port.write(b_ssi)  # Encoder SSI mode
        time.sleep(0.02)
        self.__port.flushInput()
        self.__port.write(b_ssi)  # Encoder SSI mode
        time.sleep(0.02)
        if self.__port.inWaiting() > 4:
            enc_ans = self.__port.read(self.__port.inWaiting())
            if enc_ans[-5:] == b_ssimode_ans:
                logging.debug('Encoder SSI mode on')
                self.__uart_mode = False
            else:
                logging.error('Error: Can\'t turn SSI mode!')
        else:
            logging.error('Error: Can\'t turn SSI mode!')

    def reboot_encoder(self):
        if self.__uart_mode is True:
            self.ssi_mode()
        self.power_off()
        time.sleep(0.3)
        self.power_on()
        time.sleep(0.07)

    def port_write(self, data):
        self.__port.write(data)
        time.sleep(0.05)
        self.__port.flushInput()

    def connect_encoder(self):
        self.start()
        self.uart_mode()
        ConnectState = False
        while ConnectState is False:
            self.power_off()
            time.sleep(0.1)
            self.power_on()
            time.sleep(0.01)
            self.__port.write(b_start)
            self.__port.flushInput()
            time.sleep(0.01)
            if self.__port.inWaiting() > 3:
                enc_ans = self.__port.read(self.__port.inWaiting())
                if enc_ans[-4:] == b_ans:
                    logging.debug('Encoder connected!')
                    ConnectState = True
                    return
                else:
                    logging.error('\tERROR: Can\'t connect the Encoder! Retrying...')
            else:
                logging.error('\tERROR: Can\'t connect the Encoder!!! Retrying...')

    def set_pos(self, deg):
        data = np.int32((deg / 360 * 4096) % 4096)
        b_2 = np.uint8(data >> 4)
        b_1 = np.uint8(data << 4) | 5
        self.connect_encoder()
        set_pos_cmd = bytearray([0, b_1, b_2, 6, np.uint8(250-b_1-b_2)])
        self.__port.write(set_pos_cmd)
        time.sleep(0.02)
        self.reboot_encoder()
        return b_1, b_2

    def set_pos_rev(self, deg):
        data = np.int32((deg / 360 * 4096) % 4096)
        b_2 = np.uint8(data >> 4)
        b_1 = np.uint8(data << 4) | 13
        self.connect_encoder()
        set_pos_cmd = bytearray([0, b_1, b_2, 14, np.uint8(242-b_1-b_2)])
        self.__port.write(set_pos_cmd)
        time.sleep(0.02)
        self.reboot_encoder()
        return b_1, b_2

    def biss_read(self, adr, size):
        self.__port.flushInput()
        biss_read_data_cmd = bytearray([0, adr, size, 130, np.uint8(126-adr-size)])
        self.__port.write(biss_read_data_cmd)
        self.__port.flushInput()
        time.sleep(0.05)
        ans_arr = np.array(list(self.__port.read(self.__port.inWaiting())[4:-1]), 'uint8')
        return ans_arr

    def biss_read_data(self):
        self.start()
        self.__port.write(b_read_data)
        time.sleep(0.05)
        if self.__port.inWaiting() > 1:
            enc_ans = self.__port.read(self.__port.inWaiting()).hex()
            return enc_ans
        else:
            return False

    def biss_write(self, adr, data):
        set_pos_cmd = bytearray([0, adr, data, 131, np.uint8(125-adr-data)])
        self.__port.flushOutput()
        self.__port.write(set_pos_cmd)
        time.sleep(0.02)
        self.__port.flushInput()
        return

    def biss_registers(self):
        self.start()
        logging.debug('Reading BiSS-C registers:')
        logging.debug(self.biss_read(0, 63))  # dynamic access biss registers
        try:
            bank = self.biss_read(64, 63)  # direct access biss registers
        except IndexError:
            logging.info('BiSS registers cannot be readed. New encoder?')
            return
        logging.info(bank)
        bank = [dec2hex(i) for i in bank]
        logging.info(bank)
        logging.info(f'BiSS reg. EDS bank: {dec2hex(bank[1])}')
        logging.info(f'BiSS reg. Profile ID: {"".join(bank[2:4]).upper()}')
        logging.info(f'BiSS reg. Serial No: {"".join(bank[4:8]).upper()}')
        logging.info(f'BiSS reg. Bootloader: {"".join(bank[44:48]).upper()}')
        logging.info(f'BiSS reg. Program ID: {"".join(bank[48:52]).upper()}')
        logging.info(f'BiSS reg. Mfg. Date: {"".join(bank[52:56]).upper()}')
        logging.info(f'BiSS reg. Manufacturer ID: {"".join(bank[62:64]).upper()}')

    @retry(times=3, exceptions=(ValueError))
    def load_enc_hex(self, filename):
        self.connect_encoder()
        df = readhex(filename)
        flashend = False
        row = 0
        logging.info(f'Uploading {filename} to the encoder.')
        Page = 1
        while not flashend:
            tx_row = bytes.fromhex(df[row])
            row += 1
            if (tx_row[3] == 4) | (tx_row[3] == 0):
                self.__port.write(tx_row)
                time.sleep(0.001)
            elif (tx_row[3] == 3):
                self.__port.write(tx_row)
                time.sleep(0.001)
                start_time = time.time()
                self.__port.flushInput()
                while (time.time() - start_time < 0.4):
                    if self.__port.inWaiting() >= 9:
                        break
                logging.info('  Page ' + str(Page) + ': Done!')
                CRC_hex = df[row - 1][8:16]
                enc_upl_answ = self.__port.read(self.__port.inWaiting()).hex()
                CRC_enc_raw = enc_upl_answ[8:16].upper()
                CRC_enc = str()
                for x in range(len(CRC_enc_raw), 0, -2):
                    CRC_enc += CRC_enc_raw[x-2:x]
                if CRC_enc == CRC_hex:
                    logging.info('Verifying uploaded data - OK!')
                else:
                    logging.error(f'Verifying Page {str(Page)}: FAILED! HEX file CRC: {CRC_hex}. \
                                    Encoder CRC: {CRC_enc}')
                    raise ValueError('Uploading file error!')
                Page += 1
            elif (tx_row[3] == 1):
                flashend = True
        self.reboot_encoder()
