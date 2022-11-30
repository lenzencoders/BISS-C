'''
 _     _____ _   _ _____   _____ _   _  ____ ___  ____  _____ ____  ____  
| |   | ____| \ | |__  /  | ____| \ | |/ ___/ _ \|  _ \| ____|  _ \/ ___| 
| |   |  _| |  \| | / /   |  _| |  \| | |  | | | | | | |  _| | |_) \___ \ 
| |___| |___| |\  |/ /_   | |___| |\  | |__| |_| | |_| | |___|  _ < ___) |
|_____|_____|_| \_/____|  |_____|_| \_|\____\___/|____/|_____|_| \_|____/ 
                                                                          

To compile:
    pyinstaller --onefile -w --icon favicon.ico .\lenz_calibration_encrypt.py
    change command_to_run to point to exe file
    change interpertor to r''
    pyinstaller --onefile --collect-data sv_ttk --icon favicon.ico .\lenz_uploader.py

'''
import re
from subprocess import Popen, PIPE
from threading import Thread
from pathlib import Path
import tkinter as tk
from tkinter import PhotoImage, filedialog
from queue import Queue, Empty
import time
from tkinter.ttk import Button
import webbrowser
import sv_ttk
import os

# command_to_run = r'lenz_calibration_encrypt.exe'
command_to_run = r'lenz_calibration_encrypt.py'
# interpretor = r''
interpretor = r'python -u'
version = 'v0.4'
home_url = r'https://lenzencoders.com'
github_url = r'https://github.com/lenzencoders'
title = 'LENZ ENCODERS calibration uploader'
about_text = \
    f"""
        {title} {version}

        This is a front-end GUI for a command line uploader
        {command_to_run}.

        Ask LENZ ENCODERS (lenzencoders.com) for calibration table
        and cipher file for your encoder.

        Repository: {github_url}

    """
if os.name == 'nt':
    nt = True
    default_path = 'C:/lenzencoders/'
else:
    import sys
    nt = False
    default_path = '~/lenzencoders/'


def iter_except(function, exception):
    """Works like builtin 2-argument `iter()`, but stops on `exception`."""
    try:
        while True:
            yield function()
    except exception:
        return


class TheThread:

    def __init__(self, root, textfield):
        self.root = root
        self.textfield = textfield
        self.checks_tuple = root.checks_tuple
        self.keys_tuple = root.keys_tuple
        self.input_definition = root.input_definition
        self.readings = root.readings
        self.splits_tuple = root.splits_tuple
        self.encoder_definition = root.encoder_definition

    def starter(self, mode=None):
        self.parms = ''
        self.command_to_run = command_to_run
        self.included = False
        self.file_not_found = False
        if mode == 'connect':
            self.parms = 'connect --flow'
        else:
            self.parms = 'upload '
            for checks in self.checks_tuple:
                value = eval(checks).get()
                if value:
                    if 'dry' in self.input_definition[checks][0]:
                        self.parms += '-d '
                    elif 'included' in self.input_definition[checks][0]:
                        self.included = True
                    else:
                        self.parms += self.input_definition[checks][0] + ' '
            for key in self.keys_tuple:
                value = eval(key).get()
                if value:
                    if 'file' in self.input_definition[key][0]:
                        if 'output' not in self.input_definition[key][0] and not Path(value).is_file():
                            self.file_not_found = True
                            continue
                        else:
                            self.parms += f'{self.input_definition[key][0]} "{value}" '
                    elif 'table' in self.input_definition[key][0]:
                        if not self.included:
                            self.parms += f'{self.input_definition[key][0]} "{value}" '
                    else:
                        self.parms += f"{self.input_definition[key][0]} {value} "

        # Credits to https://stackoverflow.com/a/32682520/19909906 for threading code
        # launch thread to read the subprocess output
        #   (put the subprocess output into the queue in a background thread,
        #    get output from the queue in the GUI thread.
        #    Output chain: process.readline -> queue -> textfield)
        q = Queue(maxsize=1024)  # limit output buffering (may stall subprocess)

        if not self.file_not_found:
            if self.allow():
                if not Path(self.command_to_run).is_file():
                    self.textfield_insert(f"\n=== Script not found: {self.command_to_run}\n")
                    # q.put(f"\n=== Script not found: {self.command_to_run}\n")
                    # self.update(q)  # start update loop
                    return
                else:
                    q.put(f"\n=== Starting {self.command_to_run}\n")
                    self.command_to_run = interpretor + ' ' + self.command_to_run + ' ' + self.parms
                    print(f'Executing {self.command_to_run}')
                    import shlex
                    # if isinstance(self.command_to_run , unicode):
                    #     cmd = self.command_to_run.encode('utf8')
                    args = shlex.split(self.command_to_run)  # know-how to kill process and not using shell=True
                    self.process = Popen(args, stdout=PIPE, stderr=PIPE, shell=False)
                self.update(q)  # start update loop
            else:
                self.textfield_insert('=== Another process is still runing! Please stop it first. \n')
                return
        else:
            self.textfield_insert('=== One of the input file not found. Aborting!\n')
            return
        t = Thread(target=self.reader_thread, args=[q])
        t.daemon = True  # close pipe if GUI process exits
        t.start()

    def reader_thread(self, q):
        """Read subprocess output and put it into the queue."""
        try:
            with self.process.stdout as pipe:
                for line in iter(pipe.readline, b''):
                    q.put(line)
        finally:
            pass
            # q.put(None)
        try:
            with self.process.stderr as pipe:
                for line in iter(pipe.readline, b''):
                    q.put(line)
        finally:
            q.put(None)

    def update(self, q):
        """Update GUI with items from the queue."""
        self.silent = False
        for line in iter_except(q.get_nowait, Empty):  # display all content
            if line is None:
                return
            else:
                keyword = str(line).split(':')
                for key in self.encoder_definition:
                    try:
                        if self.encoder_definition[key][2] in str(keyword[0]):
                            # replace \xc2\xb0 or \xb0 to degree symbol
                            s = re.sub('\\\\xc2\\\\xb0|\\\\xb0', '\N{DEGREE SIGN}', keyword[1])
                            s = re.sub('n\'', '', s)                             # remove n'
                            s = re.sub('\\\\\' ', '\' ', s)                      # replace \' to '
                            s = re.sub('\\\\|t|r', '', s)                        # remove \, t, r
                            s = re.sub(r'^\s+', '', s)                           # remove leading white spaces
                            tk.StringVar(value=f'{s}', name=key)
                            self.silent = True if self.encoder_definition[key][6] == 'silent' else False
                    except IndexError:                       # because of this^^^
                        pass
                not self.silent and self.textfield_insert(line)
                break  # display no more than one line per 10 milliseconds
        self.root.after(10, self.update, q)  # schedule next update

    def stop(self):
        try:    # if there is no running subprocess
            self.before = self.process.poll()
            if nt:
                if interpretor:
                    os.system(f'taskkill /f /im {interpretor}')
                else:
                    os.system(f'taskkill /f /im {command_to_run}')
            self.process.kill()
            time.sleep(0.1)  # wait for 'None and 1' to check if the process has been killed
            # print(self.before, self.process.poll())
            if self.process.poll() and self.before is None:
                self.textfield_insert("=== Killed the process!\n")
        except AttributeError:
            pass

    def textfield_insert(self, text):
        self.textfield.insert(tk.END, text)
        self.textfield.see("end")

    def quit(self):
        self.stop()
        self.root.destroy()

    def allow(self):
        try:
            return False if self.process.poll() is None else True
        except AttributeError:
            return True


class AboutPopup():
    def __init__(self, master):
        about_top = tk.Toplevel(master)
        if nt:
            if Path('favicon.ico').is_file():
                about_top.iconbitmap('favicon.ico')
        else:
            if Path('favicon.png').is_file():
                program_directory = sys.path[0]
                about_top.iconphoto(True, PhotoImage(file=os.path.join(program_directory, "favicon.png")))
        popup_x = master.winfo_rootx() + 150
        popup_y = master.winfo_rooty() + 150
        w = 450
        h = about_text.count('\n')*20 + 40
        about_top.geometry(f'{w}x{h}')
        about_top.geometry(f'+{popup_x}+{popup_y}')
        about_top.title("About")
        about_top.resizable(0, 0)
        about_label = tk.Label(about_top, text=about_text, font=('Segoe UI', 10), justify=tk.LEFT)
        about_label.place(x=80, y=80)
        about_label.grid(row=0, columnspan=5, column=0, sticky=tk.NW)
        close_about = Button(about_top, text='OK', command=about_top.destroy)
        close_about.grid(row=1, column=1, padx=3, sticky=tk.NSEW)
        web_button = Button(about_top, text="WEB", command=lambda: webbrowser.open(home_url))
        web_button.grid(row=1, column=2, padx=3, sticky=tk.NSEW)
        web_button = Button(about_top, text="GitHub", command=lambda: webbrowser.open(github_url))
        web_button.grid(row=1, column=3, padx=3, sticky=tk.NSEW)


class LenzGUI(tk.Tk):

    entries = {}
    buttons = {}
    readings = {}
    keys_tuple = tuple()
    checks_tuple = tuple()
    input_definition = {  # parms for mode == upload
        'original_ctable': ('--original_ctable', 'Calibration Table', f'{default_path}lib_FullCal_diftable2.csv', 80,
                            'Calibration table in csv provided by Lenz Encoders', 'open', 'csv'),
        'included':        ('included', 'self.included_command', '', 0,
                            'Original calibration data is summed with new diftable', ''),
        'input_file':      ('--input_file', 'Input File', f'{default_path}lib_FullCal_diftable2.csv', 80,
                            'Diftable in csv file containing calibration data you want uploaded', 'open', 'csv'),
        'output_file':     ('--output_file', 'Output File', f'{default_path}fullcal.hex', 80,
                            'Output to save encrypted hex file', 'save', 'hex'),
        'ciph_file':       ('--ciph_file', 'Cipher File', f'{default_path}lenz_ciph12.bin', 80,
                            'The cipher file provided by Lenz Encoders', 'open', 'bin'),
        'dryrun':          ('--dry-run', 'lambda: None', '', 0,
                            'Do a dry run without having to make changes to the encoder', ''),
        # key: cmd arg or 'included' flag name, Label or command for checkbutton, entry default value,
        # width or 0 for checkbox sign, null (description that is not used), save or open file flag, file extension

    }
    encoder_definition = {
        'serial_number': ('', 'Serial Number',   'Serial No',       20, '', '', ''),
        'bootloader':    ('', 'Bootloader',      'Bootloader',      20, '', '', ''),
        'mfgdate':       ('', 'Mfg Date',        'Mfg. Date',       20, '', '', ''),
        'program':       ('', 'Program',         'Program',         20, '', '', ''),
        'manufacturer':  ('', 'Manufacturer ID', 'Manufacturer ID', 20, '', '', ''),
        'angle':         ('', 'Angle',           '[03 00 00 94',    20, '', '', 'silent'),
        # key: null, Label text, parse str, width, null, null, silent flag
    }

    def __init__(self):
        super().__init__()
        print(f'{title} {version}')
        self = self
        self.resizable(0, 0)  # lock resize
        self.title(title)
        self.columnconfigure(0, weight=1)
        self.columnconfigure(1, weight=1)
        self.columnconfigure(2, weight=1)
        sv_ttk.set_theme("light")

        if nt:
            if Path('favicon.ico').is_file():
                self.iconbitmap('favicon.ico')
        else:
            if Path('favicon.png').is_file():
                program_directory = sys.path[0]
                self.iconphoto(True, PhotoImage(file=os.path.join(program_directory, "favicon.png")))

        r = 0
        title_label = tk.Label(self, text=title, font=('Segoe UI', 14))
        title_label.grid(row=r, column=0, columnspan=22, pady=10, padx=10, sticky=tk.W)
        r += 1
        keys = []
        checks = []  # checkbuttons
        splits = []  # splits for readings
        for key in self.input_definition:
            layout_def = self.input_definition[key]
            if layout_def[3] == 0:
                default_check = 1 if 'dry' in key else 0  # dryrun by default
                globals()[key] = tk.IntVar(name=key, value=default_check)
                tk.Checkbutton(self, text=layout_def[4], variable=key,
                               command=eval(layout_def[1]),
                               font=('Segoe UI', 10)).grid(row=r, column=2, pady=3, sticky=tk.W)
                checks.append(key)
            else:
                globals()[key] = tk.StringVar(value=layout_def[2], name=key)  # item text as a variable name
                keys.append(key)
                # exec(layout_def[0] + '= tk.StringVar()') # the same, but not recommended
                tk.Label(self, text=layout_def[1],  font=('Segoe UI', 10)).grid(row=r, column=1, sticky=tk.E, padx=5)
                self.entries[key] = tk.Entry(self, textvariable=key, relief=tk.SUNKEN, width=layout_def[3],
                                             font=('Segoe UI', 10))
                self.entries[key].grid(row=r, column=2, sticky=tk.W)
                if layout_def[5] == 'open':
                    self.buttons[key] = Button(self, text="Open",
                                               command=lambda button=key,
                                               ftype=layout_def[6]: self.fdialog(ftype, button))
                    self.buttons[key].grid(row=r, column=3, padx=10, pady=3, sticky=tk.NSEW)
                elif layout_def[5] == 'save':
                    self.buttons[key] = Button(self, text="Save As",
                                               command=lambda button=key,
                                               ftype=layout_def[6]: self.fdialog(ftype, button))
                    self.buttons[key].grid(row=r, column=3, padx=10, pady=3, sticky=tk.NSEW)
            r += 1
        self.keys_tuple = tuple(keys)
        self.checks_tuple = tuple(checks)
        rr = 1
        for key in self.encoder_definition:
            layout_def = self.encoder_definition[key]
            tk.Label(self, text=layout_def[1],
                     width=layout_def[3],
                     font=('Segoe UI', 10)).grid(row=rr, column=4, sticky=tk.E)
            globals()[key] = tk.StringVar(value=layout_def[1], name=key)
            splits.append(key)
            self.readings[key] = tk.Entry(self, textvariable=key, relief=tk.SUNKEN, width=layout_def[3],
                                          font=('Segoe UI', 10), state=tk.DISABLED, disabledforeground='#333333')
            self.readings[key].grid(row=rr, column=6, padx=10,  sticky=tk.W)
            rr += 1
        self.splits_tuple = tuple(splits)
        textfield = tk.Text(self, font=('Consolas', 10), bg='#282a34', fg='white')
        textfield_height = 32
        textfield.grid(columnspan=3, rowspan=textfield_height, row=r, column=1, pady=10, padx=10, sticky='WE')
        vertscrollbar = tk.Scrollbar(self, orient='vertical', command=textfield.yview)
        vertscrollbar.grid(row=r, column=3, rowspan=32, pady=10, padx=10, sticky='NSE')
        textfield.configure(yscrollcommand=vertscrollbar.set)

        app = TheThread(self, textfield)
       
        r += 1
        button1 = Button(self, text="Connect", command=lambda: app.starter('connect'))
        button1.grid(row=r, column=4, padx=10, pady=3, sticky=tk.NSEW)
        button2 = Button(self, text="Upload", command=app.starter)
        button2.grid(row=r+1, column=4, padx=10, pady=3, sticky=tk.NSEW)
        button3 = Button(self, text="Stop", command=app.stop)
        button3.grid(row=r+2, column=4, padx=10, pady=3, sticky=tk.NSEW)
        # button4 = Button(self, text="Web", command=lambda: webbrowser.open(home_url))
        # button4.grid(row=r+3, column=4, padx=10, pady=3, sticky=tk.NSEW)
        button5 = Button(self, text="About", command=lambda: AboutPopup(self))
        button5.grid(row=r+3, column=4, padx=10, pady=3, sticky=tk.NSEW)

        r += textfield_height
        bottomtext = tk.Label(self, text=f"{version} lenzencoders.com", font=('Segoe UI', 8))
        bottomtext.grid(row=r, column=0, columnspan=22, pady=5, padx=10, sticky=tk.SW)
        self.protocol("WM_DELETE_WINDOW", app.quit)  # stop the thread on quit

    def included_command(self):
        # activate/deactivate original calibration entry and button
        self.entries['original_ctable'].config(state=tk.DISABLED) if included.get() \
            else self.entries['original_ctable'].config(state=tk.NORMAL)
        self.buttons['original_ctable'].config(state=tk.DISABLED) if included.get() \
            else self.buttons['original_ctable'].config(state=tk.NORMAL)

    def fdialog(self, ftype, button):
        filetypes = (
            (f'{ftype} files', f'*.{ftype}'),
            ('All files', '*.*')
        )
        if ftype == 'hex':
            filename = filedialog.asksaveasfilename(
                title='Save a file',
                filetypes=filetypes)
        else:
            filename = filedialog.askopenfilename(
                title='Open a file',
                #  initialdir='/',
                filetypes=filetypes)
        if filename:
            self.entries[button].delete(0, tk.END)
            self.entries[button].insert(0, filename)


root = LenzGUI()
root.mainloop()
