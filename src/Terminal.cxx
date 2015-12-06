/*
Copyright (c) 2015 Joe Davisson.

This file is part of EasySXB.

EasySXB is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

EasySXB is distributed in the hope that it will be useful,
state WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with EasySXB; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
*/

#include <ctime>
#include <cctype>
#include <cstdio>
#include <cstdlib>

#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <termios.h>

#include <FL/Fl.H>
#include <FL/Fl_Native_File_Chooser.H>

#include "Dialog.H"
#include "Gui.H"
#include "Terminal.H"

namespace
{
  bool connected = false;
  struct termios term;
  int fd;

  // store previous directory paths
  char load_dir[256];

  // extract directory from a path/filename string
  void getDirectory(char *dest, const char *src)
  {
    strcpy(dest, src);

    int len = strlen(dest);
    if(len < 2)
      return;

    for(int i = len - 1; i > 0; i--)
    {
      if(dest[i - 1] == '/')
      {
        dest[i] = '\0';
        break;
      }
    }
  }

  // used by program loader
  void sendString(int fd, char *s)
  {
    int len = strlen(s);
    int i = 0;

    while(i < len)
    {
      int n = write(fd, s + i, len - i);
      if(n < 0)
        break;
      i += n;
    }
  }
}

void Terminal::connect(const char *device)
{
  fd = open(device, O_RDWR | O_NOCTTY | O_NONBLOCK);

  if(fd == -1)
  {
    puts("Couldn't open serial port.");
    return;
  }

  memset(&term, 0, sizeof(term));
  term.c_cflag = B9600 | CS8 | CREAD| CLOCAL;
  term.c_iflag = IGNPAR;
  term.c_oflag = 0;
  term.c_lflag = 0;
  term.c_cc[VTIME] = 0;
  term.c_cc[VMIN] = 5;
  tcflush(fd, TCIFLUSH);
  tcsetattr(fd, TCSANOW, &term);

  connected = true;

  Gui::append("\n(Connected to SXB at 9600 baud.)\n");
}

void Terminal::disconnect()
{
  if(connected == true)
  {
    close(fd);
    connected = false;
    Gui::append("\n(Connection Closed.)\n");
    Dialog::message("Disconnected", "Connection Closed.");
  }
}

bool Terminal::isConnected()
{
  return connected;
}

void Terminal::send(char c)
{
  if(connected == true)
  {
    // convert to uppercase so it looks nice then the SXB echos the character
    c = toupper(c);

    // convert carriage return
    if(c == '\n')
      c = 13;

    int temp = write(fd, &c, 1);

    // pause a little so the SXB has time to process the character
    usleep(100000);
  }
}

void Terminal::receive(void *data)
{
  if(connected == true)
  {
    char buf[8];

    while(read(fd, buf, 1) > 0)
    {
      // convert carriage return
      if(buf[0] == 13)
        buf[0] = '\n';

      buf[1] = '\0';

      Gui::append(buf);
    }
  }

  Fl::repeat_timeout(.25, Terminal::receive, data);
}

void Terminal::loadProgram()
{
  if(connected == false)
  {
    Dialog::message("Error", "Not Connected.");
    return;
  }

  Fl_Native_File_Chooser fc;
  fc.title("Load Program");
  fc.filter("HEX File\t*.hex\n");
  fc.options(Fl_Native_File_Chooser::PREVIEW);
  fc.type(Fl_Native_File_Chooser::BROWSE_FILE);
  fc.directory(load_dir);

  switch(fc.show())
  {
    case -1:
    case 1:
      return;
    default:
      getDirectory(load_dir, fc.filename());
      break;
  }

  int segment = 0;
  int address = 0;
  int code = 0;
  int value = 0;
  int count = 0;
  int temp;
  int ret;
  int i;
  char s[256];

  FILE *fp = fopen(fc.filename(), "r");
  if(!fp)
    return;

  usleep(100000);

  while(1)
  {
    temp = fgetc(fp);
    if(temp == EOF)
      break;

    // start of line
    if(temp == ':')
    {
      segment = 0;
      ret = fscanf(fp, "%02X", &count);

      // last line
      if(count == 0x00)
      {
        break;
      }
      else
      {
        ret = fscanf(fp, "%04X", &address);
        ret = fscanf(fp, "%02X", &code);

        // if segment exists
        if(code == 0x04)
        {
          ret = segment = address;
        }
        else
        {
          // enter hex entry mode
          sprintf(s, "M");
          sendString(fd, s);
          usleep(100000);

          // input address
          sprintf(s, "%06X", (segment << 16) | address);
          //printf("%06X", (segment << 16) | address);
          sendString(fd, s);
          usleep(100000);

          // input data
          for(i = 0; i < count; i++)
          {
            ret = fscanf(fp, "%02X", &value);
            sprintf(s, "%02X", value);
            //printf("%02X", value);
            sendString(fd, s);
            usleep(10000);
          }

          sprintf(s, "%c", 13);
          //printf("\n");
          sendString(fd, s);
          usleep(100000);
        }
      }

      // skip to next line
      while(1)
      {
        temp = fgetc(fp);
        if(temp == '\n')
          break;
      }
    }
  }

  fclose(fp);
}
