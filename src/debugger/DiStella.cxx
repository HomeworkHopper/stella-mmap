//============================================================================
//
//   SSSS    tt          lll  lll       
//  SS  SS   tt           ll   ll        
//  SS     tttttt  eeee   ll   ll   aaaa 
//   SSSS    tt   ee  ee  ll   ll      aa
//      SS   tt   eeeeee  ll   ll   aaaaa  --  "An Atari 2600 VCS Emulator"
//  SS  SS   tt   ee      ll   ll  aa  aa
//   SSSS     ttt  eeeee llll llll  aaaaa
//
// Copyright (c) 1995-2010 wby Bradford W. Mott and the Stella team
//
// See the file "license" for information on usage and redistribution of
// this file, and for a DISCLAIMER OF ALL WARRANTIES.
//
// $Id$
//============================================================================

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "bspf.hxx"
#include "Debugger.hxx"
#include "DiStella.hxx"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
DiStella::DiStella(CartDebug::DisassemblyList& list, uInt16 start,
                   bool autocode)
  : labels(NULL) /* array of information about addresses-- can be from 2K-48K bytes in size */
{
  while(!myAddressQueue.empty())
    myAddressQueue.pop();

  myAppData.start     = 0x0;
  myAppData.length    = 4096;
  myAppData.end       = 0x0FFF;

  /*====================================*/
  /* Allocate memory for "labels" variable */
  labels=(uInt8*) malloc(myAppData.length);
  if (labels == NULL)
  {
    fprintf (stderr, "Malloc failed for 'labels' variable\n");
    return;
  }
  memset(labels,0,myAppData.length);

  /*============================================
    The offset is the address where the code segment
    starts.  For a 4K game, it is usually 0xf000,
    which would then have the code data end at 0xffff,
    but that is not necessarily the case.  Because the
    Atari 2600 only has 13 address lines, it's possible
    that the "code" can be considered to start in a lot
    of different places.  So, we use the start
    address as a reference to determine where the
    offset is, logically-anded to produce an offset
    that is a multiple of 4K.

    Example:
      Start address = $D973, so therefore
      Offset to code = $D000
      Code range = $D000-$DFFF
  =============================================*/
  myOffset = (start - (start % 0x1000));

  myAddressQueue.push(start);

  if(autocode)
  {
    while(!myAddressQueue.empty())
    {
      myPC = myAddressQueue.front();
      myPCBeg = myPC;
      myAddressQueue.pop();
      disasm(list, myPC, 1);
      for (uInt32 k = myPCBeg; k <= myPCEnd; k++)
        mark(k, REACHABLE);
    }
    
    for (int k = 0; k <= myAppData.end; k++)
    {
      if (!check_bit(labels[k], REACHABLE))
        mark(k+myOffset, DATA);
    }
  }

  // Second pass
  disasm(list, myOffset, 2);

  // Third pass
  disasm(list, myOffset, 3);

  free(labels);  /* Free dynamic memory before program ends */
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
DiStella::~DiStella()
{
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void DiStella::disasm(CartDebug::DisassemblyList& list, uInt32 distart, int pass)
{
#define HEX4 uppercase << hex << setw(4) << setfill('0')
#define HEX2 uppercase << hex << setw(2) << setfill('0')

  uInt8 op, d1, opsrc;
  uInt32 ad;
  short amode;
  int bytes=0, labfound=0, addbranch=0;
  char linebuff[256],nextline[256], nextlinebytes[256];
  strcpy(linebuff,"");
  strcpy(nextline,"");
  strcpy(nextlinebytes,"");
  myBuf.str("");

  /* pc=myAppData.start; */
  myPC = distart - myOffset;
  while(myPC <= myAppData.end)
  {
    if(check_bit(labels[myPC], GFX))
      /* && !check_bit(labels[myPC], REACHABLE))*/
    {
      if (pass == 2)
        mark(myPC+myOffset,VALID_ENTRY);
      else if (pass == 3)
      {
        if (check_bit(labels[myPC],REFERENCED))
          myBuf << HEX4 << myPC+myOffset << "'L'" << HEX4 << myPC+myOffset << "'";
        else
          myBuf << HEX4 << myPC+myOffset << "'     '";

        myBuf << ".byte $" << HEX2 << (int)Debugger::debugger().peek(myPC+myOffset) << " ; ";
        showgfx(Debugger::debugger().peek(myPC+myOffset));
        myBuf << " $" << HEX4 << myPC+myOffset;
        addEntry(list);
      }
      myPC++;
    }
    else if (check_bit(labels[myPC], DATA) && !check_bit(labels[myPC], GFX))
        /* && !check_bit(labels[myPC],REACHABLE)) {  */
    {
      mark(myPC+myOffset, VALID_ENTRY);
      if (pass == 3)
      {
        bytes = 1;
        myBuf << HEX4 << myPC+myOffset << "'L" << myPC+myOffset << "'.byte "
              << "$" << HEX2 << (int)Debugger::debugger().peek(myPC+myOffset);
      }
      myPC++;

      while (check_bit(labels[myPC], DATA) && !check_bit(labels[myPC], REFERENCED)
             && !check_bit(labels[myPC], GFX) && pass == 3 && myPC <= myAppData.end)
      {
        if (pass == 3)
        {
          bytes++;
          if (bytes == 17)
          {
            addEntry(list);
            myBuf << "    '     '.byte $" << HEX2 << (int)Debugger::debugger().peek(myPC+myOffset);
            bytes = 1;
          }
          else
            myBuf << ",$" << HEX2 << (int)Debugger::debugger().peek(myPC+myOffset);
        }
        myPC++;
      }

      if (pass == 3)
      {
        addEntry(list);
        myBuf << "    '     ' ";
        addEntry(list);
      }
    }
    else
    {
      op = Debugger::debugger().peek(myPC+myOffset);
      /* version 2.1 bug fix */
      if (pass == 2)
        mark(myPC+myOffset, VALID_ENTRY);
      else if (pass == 3)
      {
        if (check_bit(labels[myPC], REFERENCED))
          myBuf << HEX4 << myPC+myOffset << "'L" << HEX4 << myPC+myOffset << "'";
        else
          myBuf << HEX4 << myPC+myOffset << "'     '";
      }

      amode = ourLookup[op].addr_mode;
      myPC++;

      if (ourLookup[op].mnemonic[0] == '.')
      {
        amode = IMPLIED;
        if (pass == 3)
        {
          sprintf(linebuff,".byte $%.2X ;",op);
          strcat(nextline,linebuff);
        }
      }

      if (pass == 1)
      {
        opsrc = ourLookup[op].source;
        /* M_REL covers BPL, BMI, BVC, BVS, BCC, BCS, BNE, BEQ
           M_ADDR = JMP $NNNN, JSR $NNNN
           M_AIND = JMP Abs, Indirect */
        if ((opsrc == M_REL) || (opsrc == M_ADDR) || (opsrc == M_AIND))
          addbranch = 1;
        else
          addbranch = 0;
      }
      else if (pass == 3)
      {
        sprintf(linebuff,"%s",ourLookup[op].mnemonic);
        strcat(nextline,linebuff);
        sprintf(linebuff,"%02X ",op);
        strcat(nextlinebytes,linebuff);
      }

      if (myPC >= myAppData.end)
      {
        switch(amode)
        {
          case ABSOLUTE:
          case ABSOLUTE_X:
          case ABSOLUTE_Y:
          case INDIRECT_X:
          case INDIRECT_Y:
          case ABS_INDIRECT:
          {
            if (pass == 3)
            {
              /* Line information is already printed; append .byte since last instruction will
                 put recompilable object larger that original binary file */
              myBuf << ".byte $" << HEX2 << op;
              addEntry(list);

              if (myPC == myAppData.end)
              {
                if (check_bit(labels[myPC],REFERENCED))
                  myBuf << HEX4 << myPC+myOffset << "'L" << HEX4 << myPC+myOffset << "'";
                else
                  myBuf << HEX4 << myPC+myOffset << "'     '";

                op = Debugger::debugger().peek(myPC+myOffset);  myPC++;
                myBuf << ".byte $" << HEX2 << (int)op;
                addEntry(list);
              }
            }
            myPCEnd = myAppData.end + myOffset;
            return;
          }

          case ZERO_PAGE:
          case IMMEDIATE:
          case ZERO_PAGE_X:
          case ZERO_PAGE_Y:
          case RELATIVE:
          {
            if (myPC > myAppData.end)
            {
              if (pass == 3)
              {
                /* Line information is already printed, but we can remove the
                   Instruction (i.e. BMI) by simply clearing the buffer to print */
                strcpy(nextline,"");
                sprintf(linebuff,".byte $%.2X",op);
                strcat(nextline,linebuff);

                myBuf << nextline;
                addEntry(list);
                strcpy(nextline,"");
                strcpy(nextlinebytes,"");
              }
              myPC++;
              myPCEnd = myAppData.end + myOffset;
              return;
            }
          }

          default:
            break;
        }
      }

      /* Version 2.1 added the extensions to mnemonics */
      switch(amode)
      {
    #if 0
        case IMPLIED:
        {
          if (op == 0x40 || op == 0x60)
            if (pass == 3)
            {
              sprintf(linebuff,"\n");
              strcat(nextline,linebuff);
            }
          break;
        }
    #endif
        case ACCUMULATOR:
        {
          if (pass == 3)
            sprintf(linebuff,"    A");

          strcat(nextline,linebuff);
          break;
        }

        case ABSOLUTE:
        {
          ad = Debugger::debugger().dpeek(myPC+myOffset);  myPC+=2;
          labfound = mark(ad, REFERENCED);
          if (pass == 1)
          {
            if ((addbranch) && !check_bit(labels[ad & myAppData.end], REACHABLE))
            {
              if (ad > 0xfff)
                myAddressQueue.push((ad & myAppData.end) + myOffset);

              mark(ad, REACHABLE);
            }
          }
          else if (pass == 3)
          {
            if (ad < 0x100)
            {
              sprintf(linebuff,".w  ");
              strcat(nextline,linebuff);
            }
            else
            {
              sprintf(linebuff,"    ");
              strcat(nextline,linebuff);
            }
            if (labfound == 1)
            {
              sprintf(linebuff,"L%.4X",ad);
              strcat(nextline,linebuff);
              sprintf(linebuff,"%02X %02X",(ad&0xff),(ad>>8));
              strcat(nextlinebytes,linebuff);
            }
            else if (labfound == 3)
            {
              sprintf(linebuff,"%s",ourIOMnemonic[ad-0x280]);
              strcat(nextline,linebuff);
              sprintf(linebuff,"%02X %02X",(ad&0xff),(ad>>8));
              strcat(nextlinebytes,linebuff);
            }
            else if (labfound == 4)
            {
              int tmp = (ad & myAppData.end)+myOffset;
              sprintf(linebuff,"L%.4X",tmp);
              strcat(nextline,linebuff);
              sprintf(linebuff,"%02X %02X",(tmp&0xff),(tmp>>8));
              strcat(nextlinebytes,linebuff);
            }
            else
            {
              sprintf(linebuff,"$%.4X",ad);
              strcat(nextline,linebuff);
              sprintf(linebuff,"%02X %02X",(ad&0xff),(ad>>8));
              strcat(nextlinebytes,linebuff);
            }
          }
          break;
        }

        case ZERO_PAGE:
        {
          d1 = Debugger::debugger().peek(myPC+myOffset);  myPC++;
          labfound = mark(d1, REFERENCED);
          if (pass == 3)
          {
            if (labfound == 2)
            {
              sprintf(linebuff,"    %s", ourTIAMnemonic[d1]);
              strcat(nextline,linebuff);
            }
            else
            {
              sprintf(linebuff,"    $%.2X ",d1);
              strcat(nextline,linebuff);
            }
            sprintf(linebuff,"%02X", d1);
            strcat(nextlinebytes,linebuff);
          }
          break;
        }

        case IMMEDIATE:
        {
          d1 = Debugger::debugger().peek(myPC+myOffset);  myPC++;
          if (pass == 3)
          {
            sprintf(linebuff,"    #$%.2X ",d1);
            strcat(nextline,linebuff);
            sprintf(linebuff,"%02X",d1);
            strcat(nextlinebytes,linebuff);
          }
          break;
        }

        case ABSOLUTE_X:
        {
          ad = Debugger::debugger().dpeek(myPC+myOffset);  myPC+=2;
          labfound = mark(ad, REFERENCED);
          if (pass == 3)
          {
            if (ad < 0x100)
            {
              sprintf(linebuff,".wx ");
              strcat(nextline,linebuff);
            }
            else
            {
              sprintf(linebuff,"    ");
              strcat(nextline,linebuff);
            }

            if (labfound == 1)
            {
              sprintf(linebuff,"L%.4X,X",ad);
              strcat(nextline,linebuff);
              sprintf(linebuff,"%02X %02X",(ad&0xff),(ad>>8));
              strcat(nextlinebytes,linebuff);
            }
            else if (labfound == 3)
            {
              sprintf(linebuff,"%s,X",ourIOMnemonic[ad-0x280]);
              strcat(nextline,linebuff);
              sprintf(linebuff,"%02X %02X",(ad&0xff),(ad>>8));
              strcat(nextlinebytes,linebuff);
            }
            else if (labfound == 4)
            {
              int tmp = (ad & myAppData.end)+myOffset;
              sprintf(linebuff,"L%.4X,X",tmp);
              strcat(nextline,linebuff);
              sprintf(linebuff,"%02X %02X",(tmp&0xff),(tmp>>8));
              strcat(nextlinebytes,linebuff);
            }
            else
            {
              sprintf(linebuff,"$%.4X,X",ad);
              strcat(nextline,linebuff);
              sprintf(linebuff,"%02X %02X",(ad&0xff),(ad>>8));
              strcat(nextlinebytes,linebuff);
            }
          }
          break;
        }

        case ABSOLUTE_Y:
        {
          ad = Debugger::debugger().dpeek(myPC+myOffset);  myPC+=2;
          labfound = mark(ad, REFERENCED);
          if (pass == 3)
          {
            if (ad < 0x100)
            {
              sprintf(linebuff,".wy ");
              strcat(nextline,linebuff);
            }
            else
            {
              sprintf(linebuff,"    ");
              strcat(nextline,linebuff);
            }
            if (labfound == 1)
            {
              sprintf(linebuff,"L%.4X,Y",ad);
              strcat(nextline,linebuff);
              sprintf(linebuff,"%02X %02X",(ad&0xff),(ad>>8));
              strcat(nextlinebytes,linebuff);
            }
            else if (labfound == 3)
            {
              sprintf(linebuff,"%s,Y",ourIOMnemonic[ad-0x280]);
              strcat(nextline,linebuff);
              sprintf(linebuff,"%02X %02X",(ad&0xff),(ad>>8));
              strcat(nextlinebytes,linebuff);
            }
            else if (labfound == 4)
            {
              int tmp = (ad & myAppData.end)+myOffset;
              sprintf(linebuff,"L%.4X,Y",tmp);
              strcat(nextline,linebuff);
              sprintf(linebuff,"%02X %02X",(tmp&0xff),(tmp>>8));
              strcat(nextlinebytes,linebuff);
            }
            else
            {
              sprintf(linebuff,"$%.4X,Y",ad);
              strcat(nextline,linebuff);
              sprintf(linebuff,"%02X %02X",(ad&0xff),(ad>>8));
              strcat(nextlinebytes,linebuff);
            }
          }
          break;
        }

        case INDIRECT_X:
        {
          d1 = Debugger::debugger().peek(myPC+myOffset);  myPC++;
          if (pass == 3)
          {
            sprintf(linebuff,"    ($%.2X,X)",d1);
            strcat(nextline,linebuff);
            sprintf(linebuff,"%02X",d1);
            strcat(nextlinebytes,linebuff);
          }
          break;
        }

        case INDIRECT_Y:
        {
          d1 = Debugger::debugger().peek(myPC+myOffset);  myPC++;
          if (pass == 3)
          {
            sprintf(linebuff,"    ($%.2X),Y",d1);
            strcat(nextline,linebuff);
            sprintf(linebuff,"%02X",d1);
            strcat(nextlinebytes,linebuff);
          }
          break;
        }

        case ZERO_PAGE_X:
        {
          d1 = Debugger::debugger().peek(myPC+myOffset);  myPC++;
          labfound = mark(d1, REFERENCED);
          if (pass == 3)
          {
            if (labfound == 2)
            {
              sprintf(linebuff,"    %s,X", ourTIAMnemonic[d1]);
              strcat(nextline,linebuff);
            }
            else
            {
              sprintf(linebuff,"    $%.2X,X",d1);
              strcat(nextline,linebuff);
            }
          }
          sprintf(linebuff,"%02X",d1);
          strcat(nextlinebytes,linebuff);
          break;
        }

        case ZERO_PAGE_Y:
        {
          d1 = Debugger::debugger().peek(myPC+myOffset);  myPC++;
          labfound = mark(d1,REFERENCED);
          if (pass == 3)
          {
            if (labfound == 2)
            {
              sprintf(linebuff,"    %s,Y", ourTIAMnemonic[d1]);
              strcat(nextline,linebuff);
            }
            else
            {
              sprintf(linebuff,"    $%.2X,Y",d1);
              strcat(nextline,linebuff);
            }
          }
          sprintf(linebuff,"%02X",d1);
          strcat(nextlinebytes,linebuff);
          break;
        }

        case RELATIVE:
        {
          d1 = Debugger::debugger().peek(myPC+myOffset);  myPC++;
          ad = d1;
          if (d1 >= 128)
            ad = d1 - 256;

          labfound = mark(myPC+ad+myOffset, REFERENCED);
          if (pass == 1)
          {
            if ((addbranch) && !check_bit(labels[myPC+ad], REACHABLE))
            {
              myAddressQueue.push(myPC+ad+myOffset);
              mark(myPC+ad+myOffset, REACHABLE);
              /*       addressq=addq(addressq,myPC+myOffset); */
            }
          }
          else if (pass == 3)
          {
            int tmp = myPC+ad+myOffset;
            if (labfound == 1)
            {
              sprintf(linebuff,"    L%.4X",tmp);
              strcat(nextline,linebuff);
            }
            else
            {
              sprintf(linebuff,"    $%.4X",tmp);
              strcat(nextline,linebuff);
            }
            sprintf(linebuff,"%02X %02X",(tmp&0xff),(tmp>>8));
            strcat(nextlinebytes,linebuff);
          }
          break;
        }

        case ABS_INDIRECT:
        {
          ad = Debugger::debugger().dpeek(myPC+myOffset);  myPC+=2;
          labfound = mark(ad, REFERENCED);
          if (pass == 3)
          {
            if (ad < 0x100)
            {
              sprintf(linebuff,".ind ");
              strcat(nextline,linebuff);
            }
            else
            {
              sprintf(linebuff,"    ");
              strcat(nextline,linebuff);
            }
          }
          if (labfound == 1)
          {
            sprintf(linebuff,"(L%04X)",ad);
            strcat(nextline,linebuff);
            sprintf(linebuff,"%02X %02X",(ad&0xff),(ad>>8));
            strcat(nextlinebytes,linebuff);
          }
          else if (labfound == 3)
          {
            sprintf(linebuff,"(%s)",ourIOMnemonic[ad-0x280]);
            strcat(nextline,linebuff);
            sprintf(linebuff,"%02X %02X",(ad&0xff),(ad>>8));
            strcat(nextlinebytes,linebuff);
          }
          else
          {
            sprintf(linebuff,"($%04X)",ad);
            strcat(nextline,linebuff);
            sprintf(linebuff,"%02X %02X",(ad&0xff),(ad>>8));
            strcat(nextlinebytes,linebuff);
          }
          break;
        }
      } // end switch

      if (pass == 1)
      {
        if (!strcmp(ourLookup[op].mnemonic,"RTS") ||
            !strcmp(ourLookup[op].mnemonic,"JMP") ||
            /* !strcmp(ourLookup[op].mnemonic,"BRK") || */
            !strcmp(ourLookup[op].mnemonic,"RTI"))
        {
          myPCEnd = (myPC-1) + myOffset;
          return;
        }
      }
      else if (pass == 3)
      {
        myBuf << nextline;
        if (strlen(nextline) <= 15)
        {
          /* Print spaces to align cycle count data */
          for (uInt32 charcnt=0;charcnt<15-strlen(nextline);charcnt++)
            myBuf << " ";
        }
        myBuf << ";" << dec << (int)ourLookup[op].cycles << "'" << nextlinebytes;
        addEntry(list);
        if (op == 0x40 || op == 0x60)
        {
          myBuf << "    '     ' ";
          addEntry(list);
        }

        strcpy(nextline,"");
        strcpy(nextlinebytes,"");
      }
    }
  }  /* while loop */

  /* Just in case we are disassembling outside of the address range, force the myPCEnd to EOF */
  myPCEnd = myAppData.end + myOffset;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
int DiStella::mark(uInt32 address, MarkType bit)
{
  /*-----------------------------------------------------------------------
    For any given offset and code range...

    If we're between the offset and the end of the code range, we mark
    the bit in the labels array for that data.  The labels array is an
    array of label info for each code address.  If this is the case,
    return "1", else...

    We sweep for hardware/system equates, which are valid addresses,
    outside the scope of the code/data range.  For these, we mark its
    corresponding hardware/system array element, and return "2" or "3"
    (depending on which system/hardware element was accessed).  If this
    was not the case...

    Next we check if it is a code "mirror".  For the 2600, address ranges
    are limited with 13 bits, so other addresses can exist outside of the
    standard code/data range.  For these, we mark the element in the "labels"
    array that corresponds to the mirrored address, and return "4"

    If all else fails, it's not a valid address, so return 0.

    A quick example breakdown for a 2600 4K cart:
    ===========================================================
      $00-$3d =     system equates (WSYNC, etc...); mark the array's element
                    with the appropriate bit; return 2.
      $0280-$0297 = system equates (INPT0, etc...); mark the array's element
                    with the appropriate bit; return 3.
      $1000-$1FFF = CODE/DATA, mark the code/data array for the mirrored address
                    with the appropriate bit; return 4.
      $3000-$3FFF = CODE/DATA, mark the code/data array for the mirrored address
                    with the appropriate bit; return 4.
      $5000-$5FFF = CODE/DATA, mark the code/data array for the mirrored address
                    with the appropriate bit; return 4.
      $7000-$7FFF = CODE/DATA, mark the code/data array for the mirrored address
                    with the appropriate bit; return 4.
      $9000-$9FFF = CODE/DATA, mark the code/data array for the mirrored address
                    with the appropriate bit; return 4.
      $B000-$BFFF = CODE/DATA, mark the code/data array for the mirrored address
                    with the appropriate bit; return 4.
      $D000-$DFFF = CODE/DATA, mark the code/data array for the mirrored address
                    with the appropriate bit; return 4.
      $F000-$FFFF = CODE/DATA, mark the code/data array for the address
                    with the appropriate bit; return 1.
      Anything else = invalid, return 0.
    ===========================================================
  -----------------------------------------------------------------------*/

  if (address >= myOffset && address <= myAppData.end + myOffset)
  {
    labels[address-myOffset] = labels[address-myOffset] | bit;
    return 1;
  }
  else if (address >= 0 && address <= 0x3d)
  {
//    reserved[address] = 1;
    return 2;
  }
  else if (address >= 0x280 && address <= 0x297)
  {
//    ioresrvd[address-0x280] = 1;
    return 3;
  }
  else if (address > 0x1000)
  {
    /* 2K & 4K case */
    labels[address & myAppData.end] = labels[address & myAppData.end] | bit;
    return 4;
  }
  else
    return 0;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
int DiStella::check_bit(uInt8 bitflags, int i)
{
  return (int)(bitflags & i);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void DiStella::showgfx(uInt8 c)
{
  int i;

  myBuf << "|";
  for(i = 0;i < 8; i++)
  {
    if (c > 127)
      myBuf << "X";
    else
      myBuf << " ";

    c = c << 1;
  }
  myBuf << "|";
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void DiStella::addEntry(CartDebug::DisassemblyList& list)
{
  const string& line = myBuf.str();
  CartDebug::DisassemblyTag tag;

  if(line[0] == ' ')
    tag.address = 0;
  else
    myBuf >> setw(4) >> hex >> tag.address;

  if(line[5] != ' ')
    tag.label = line.substr(5, 5);

  switch(line[11])
  {
    case ' ':
      tag.disasm = " ";
      break;
    case '.':
      tag.disasm = line.substr(11);
      break;
    default:
      tag.disasm = line.substr(11, 17);
      tag.bytes  = line.substr(29);
      break;
  }
  list.push_back(tag);

  myBuf.str("");
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
const DiStella::Instruction_tag DiStella::ourLookup[256] = {
/****  Positive  ****/

  /* 00 */ { "BRK",   IMPLIED,    M_NONE, M_PC,   7 }, /* Pseudo Absolute */
  /* 01 */ { "ORA",   INDIRECT_X, M_INDX, M_AC,   6 }, /* (Indirect,X) */
  /* 02 */ { ".JAM",  IMPLIED,    M_NONE, M_NONE, 0 }, /* TILT */
  /* 03 */ { ".SLO",  INDIRECT_X, M_INDX, M_INDX, 8 },

  /* 04 */ { ".NOOP", ZERO_PAGE,  M_NONE, M_NONE, 3 },
  /* 05 */ { "ORA",   ZERO_PAGE,  M_ZERO, M_AC,   3 }, /* Zeropage */
  /* 06 */ { "ASL",   ZERO_PAGE,  M_ZERO, M_ZERO, 5 }, /* Zeropage */
  /* 07 */ { ".SLO",  ZERO_PAGE,  M_ZERO, M_ZERO, 5 },

  /* 08 */ { "PHP",   IMPLIED,     M_SR,   M_NONE, 3 },
  /* 09 */ { "ORA",   IMMEDIATE,   M_IMM,  M_AC,   2 }, /* Immediate */
  /* 0a */ { "ASL",   ACCUMULATOR, M_AC,   M_AC,   2 }, /* Accumulator */
  /* 0b */ { ".ANC",  IMMEDIATE,   M_ACIM, M_ACNC, 2 },

  /* 0c */ { ".NOOP", ABSOLUTE, M_NONE, M_NONE, 4 },
  /* 0d */ { "ORA",   ABSOLUTE, M_ABS,  M_AC,   4 }, /* Absolute */
  /* 0e */ { "ASL",   ABSOLUTE, M_ABS,  M_ABS,  6 }, /* Absolute */
  /* 0f */ { ".SLO",  ABSOLUTE, M_ABS,  M_ABS,  6 },

  /* 10 */ { "BPL",   RELATIVE,   M_REL,  M_NONE, 2 },
  /* 11 */ { "ORA",   INDIRECT_Y, M_INDY, M_AC,   5 }, /* (Indirect),Y */
  /* 12 */ { ".JAM",  IMPLIED,    M_NONE, M_NONE, 0 }, /* TILT */
  /* 13 */ { ".SLO",  INDIRECT_Y, M_INDY, M_INDY, 8 },

  /* 14 */ { ".NOOP", ZERO_PAGE_X, M_NONE, M_NONE, 4 },
  /* 15 */ { "ORA",   ZERO_PAGE_X, M_ZERX, M_AC,   4 }, /* Zeropage,X */
  /* 16 */ { "ASL",   ZERO_PAGE_X, M_ZERX, M_ZERX, 6 }, /* Zeropage,X */
  /* 17 */ { ".SLO",  ZERO_PAGE_X, M_ZERX, M_ZERX, 6 },

  /* 18 */ { "CLC",   IMPLIED,    M_NONE, M_FC,   2 },
  /* 19 */ { "ORA",   ABSOLUTE_Y, M_ABSY, M_AC,   4 }, /* Absolute,Y */
  /* 1a */ { ".NOOP", IMPLIED,    M_NONE, M_NONE, 2 },
  /* 1b */ { ".SLO",  ABSOLUTE_Y, M_ABSY, M_ABSY, 7 },

  /* 1c */ { ".NOOP", ABSOLUTE_X, M_NONE, M_NONE, 4 },
  /* 1d */ { "ORA",   ABSOLUTE_X, M_ABSX, M_AC,   4 }, /* Absolute,X */
  /* 1e */ { "ASL",   ABSOLUTE_X, M_ABSX, M_ABSX, 7 }, /* Absolute,X */
  /* 1f */ { ".SLO",  ABSOLUTE_X, M_ABSX, M_ABSX, 7 },

  /* 20 */ { "JSR",   ABSOLUTE,   M_ADDR, M_PC,   6 },
  /* 21 */ { "AND",   INDIRECT_X, M_INDX, M_AC,   6 }, /* (Indirect ,X) */
  /* 22 */ { ".JAM",  IMPLIED,    M_NONE, M_NONE, 0 }, /* TILT */
  /* 23 */ { ".RLA",  INDIRECT_X, M_INDX, M_INDX, 8 },

  /* 24 */ { "BIT",   ZERO_PAGE, M_ZERO, M_NONE, 3 }, /* Zeropage */
  /* 25 */ { "AND",   ZERO_PAGE, M_ZERO, M_AC,   3 }, /* Zeropage */
  /* 26 */ { "ROL",   ZERO_PAGE, M_ZERO, M_ZERO, 5 }, /* Zeropage */
  /* 27 */ { ".RLA",  ZERO_PAGE, M_ZERO, M_ZERO, 5 },

  /* 28 */ { "PLP",   IMPLIED,     M_NONE, M_SR,   4 },
  /* 29 */ { "AND",   IMMEDIATE,   M_IMM,  M_AC,   2 }, /* Immediate */
  /* 2a */ { "ROL",   ACCUMULATOR, M_AC,   M_AC,   2 }, /* Accumulator */
  /* 2b */ { ".ANC",  IMMEDIATE,   M_ACIM, M_ACNC, 2 },

  /* 2c */ { "BIT",   ABSOLUTE, M_ABS, M_NONE, 4 }, /* Absolute */
  /* 2d */ { "AND",   ABSOLUTE, M_ABS, M_AC,   4 }, /* Absolute */
  /* 2e */ { "ROL",   ABSOLUTE, M_ABS, M_ABS,  6 }, /* Absolute */
  /* 2f */ { ".RLA",  ABSOLUTE, M_ABS, M_ABS,  6 },

  /* 30 */ { "BMI",   RELATIVE,   M_REL,  M_NONE, 2 },
  /* 31 */ { "AND",   INDIRECT_Y, M_INDY, M_AC,   5 }, /* (Indirect),Y */
  /* 32 */ { ".JAM",  IMPLIED,    M_NONE, M_NONE, 0 }, /* TILT */
  /* 33 */ { ".RLA",  INDIRECT_Y, M_INDY, M_INDY, 8 },

  /* 34 */ { ".NOOP", ZERO_PAGE_X, M_NONE, M_NONE, 4 },
  /* 35 */ { "AND",   ZERO_PAGE_X, M_ZERX, M_AC,   4 }, /* Zeropage,X */
  /* 36 */ { "ROL",   ZERO_PAGE_X, M_ZERX, M_ZERX, 6 }, /* Zeropage,X */
  /* 37 */ { ".RLA",  ZERO_PAGE_X, M_ZERX, M_ZERX, 6 },

  /* 38 */ { "SEC",   IMPLIED,    M_NONE, M_FC,   2 },
  /* 39 */ { "AND",   ABSOLUTE_Y, M_ABSY, M_AC,   4 }, /* Absolute,Y */
  /* 3a */ { ".NOOP", IMPLIED,    M_NONE, M_NONE, 2 },
  /* 3b */ { ".RLA",  ABSOLUTE_Y, M_ABSY, M_ABSY, 7 },

  /* 3c */ { ".NOOP", ABSOLUTE_X, M_NONE, M_NONE, 4 },
  /* 3d */ { "AND",   ABSOLUTE_X, M_ABSX, M_AC,   4 }, /* Absolute,X */
  /* 3e */ { "ROL",   ABSOLUTE_X, M_ABSX, M_ABSX, 7 }, /* Absolute,X */
  /* 3f */ { ".RLA",  ABSOLUTE_X, M_ABSX, M_ABSX, 7 },

  /* 40 */ { "RTI" ,  IMPLIED,    M_NONE, M_PC,   6 },
  /* 41 */ { "EOR",   INDIRECT_X, M_INDX, M_AC,   6 }, /* (Indirect,X) */
  /* 42 */ { ".JAM",  IMPLIED,    M_NONE, M_NONE, 0 }, /* TILT */
  /* 43 */ { ".SRE",  INDIRECT_X, M_INDX, M_INDX, 8 },

  /* 44 */ { ".NOOP", ZERO_PAGE, M_NONE, M_NONE, 3 },
  /* 45 */ { "EOR",   ZERO_PAGE, M_ZERO, M_AC,   3 }, /* Zeropage */
  /* 46 */ { "LSR",   ZERO_PAGE, M_ZERO, M_ZERO, 5 }, /* Zeropage */
  /* 47 */ { ".SRE",  ZERO_PAGE, M_ZERO, M_ZERO, 5 },

  /* 48 */ { "PHA",   IMPLIED,     M_AC,   M_NONE, 3 },
  /* 49 */ { "EOR",   IMMEDIATE,   M_IMM,  M_AC,   2 }, /* Immediate */
  /* 4a */ { "LSR",   ACCUMULATOR, M_AC,   M_AC,   2 }, /* Accumulator */
  /* 4b */ { ".ASR",  IMMEDIATE,   M_ACIM, M_AC,   2 }, /* (AC & IMM) >>1 */

  /* 4c */ { "JMP",   ABSOLUTE, M_ADDR, M_PC,  3 }, /* Absolute */
  /* 4d */ { "EOR",   ABSOLUTE, M_ABS,  M_AC,  4 }, /* Absolute */
  /* 4e */ { "LSR",   ABSOLUTE, M_ABS,  M_ABS, 6 }, /* Absolute */
  /* 4f */ { ".SRE",  ABSOLUTE, M_ABS,  M_ABS, 6 },

  /* 50 */ { "BVC",   RELATIVE,   M_REL,  M_NONE, 2 },
  /* 51 */ { "EOR",   INDIRECT_Y, M_INDY, M_AC,   5 }, /* (Indirect),Y */
  /* 52 */ { ".JAM",  IMPLIED,    M_NONE, M_NONE, 0 }, /* TILT */
  /* 53 */ { ".SRE",  INDIRECT_Y, M_INDY, M_INDY, 8 },

  /* 54 */ { ".NOOP", ZERO_PAGE_X, M_NONE, M_NONE, 4 },
  /* 55 */ { "EOR",   ZERO_PAGE_X, M_ZERX, M_AC,   4 }, /* Zeropage,X */
  /* 56 */ { "LSR",   ZERO_PAGE_X, M_ZERX, M_ZERX, 6 }, /* Zeropage,X */
  /* 57 */ { ".SRE",  ZERO_PAGE_X, M_ZERX, M_ZERX, 6 },

  /* 58 */ { "CLI",   IMPLIED,    M_NONE, M_FI,   2 },
  /* 59 */ { "EOR",   ABSOLUTE_Y, M_ABSY, M_AC,   4 }, /* Absolute,Y */
  /* 5a */ { ".NOOP", IMPLIED,    M_NONE, M_NONE, 2 },
  /* 5b */ { ".SRE",  ABSOLUTE_Y, M_ABSY, M_ABSY, 7 },

  /* 5c */ { ".NOOP", ABSOLUTE_X, M_NONE, M_NONE, 4 },
  /* 5d */ { "EOR",   ABSOLUTE_X, M_ABSX, M_AC,   4 }, /* Absolute,X */
  /* 5e */ { "LSR",   ABSOLUTE_X, M_ABSX, M_ABSX, 7 }, /* Absolute,X */
  /* 5f */ { ".SRE",  ABSOLUTE_X, M_ABSX, M_ABSX, 7 },

  /* 60 */ { "RTS",   IMPLIED,    M_NONE, M_PC,   6 },
  /* 61 */ { "ADC",   INDIRECT_X, M_INDX, M_AC,   6 }, /* (Indirect,X) */
  /* 62 */ { ".JAM",  IMPLIED,    M_NONE, M_NONE, 0 }, /* TILT */
  /* 63 */ { ".RRA",  INDIRECT_X, M_INDX, M_INDX, 8 },

  /* 64 */ { ".NOOP", ZERO_PAGE, M_NONE, M_NONE, 3 },
  /* 65 */ { "ADC",   ZERO_PAGE, M_ZERO, M_AC,   3 }, /* Zeropage */
  /* 66 */ { "ROR",   ZERO_PAGE, M_ZERO, M_ZERO, 5 }, /* Zeropage */
  /* 67 */ { ".RRA",  ZERO_PAGE, M_ZERO, M_ZERO, 5 },

  /* 68 */ { "PLA",   IMPLIED,     M_NONE, M_AC, 4 },
  /* 69 */ { "ADC",   IMMEDIATE,   M_IMM,  M_AC, 2 }, /* Immediate */
  /* 6a */ { "ROR",   ACCUMULATOR, M_AC,   M_AC, 2 }, /* Accumulator */
  /* 6b */ { ".ARR",  IMMEDIATE,   M_ACIM, M_AC, 2 }, /* ARR isn't typo */

  /* 6c */ { "JMP",   ABS_INDIRECT, M_AIND, M_PC,  5 }, /* Indirect */
  /* 6d */ { "ADC",   ABSOLUTE,     M_ABS,  M_AC,  4 }, /* Absolute */
  /* 6e */ { "ROR",   ABSOLUTE,     M_ABS,  M_ABS, 6 }, /* Absolute */
  /* 6f */ { ".RRA",  ABSOLUTE,     M_ABS,  M_ABS, 6 },

  /* 70 */ { "BVS",   RELATIVE,   M_REL,  M_NONE, 2 },
  /* 71 */ { "ADC",   INDIRECT_Y, M_INDY, M_AC,   5 }, /* (Indirect),Y */
  /* 72 */ { ".JAM",  IMPLIED,    M_NONE, M_NONE, 0 }, /* TILT relative? */
  /* 73 */ { ".RRA",  INDIRECT_Y, M_INDY, M_INDY, 8 },

  /* 74 */ { ".NOOP", ZERO_PAGE_X, M_NONE, M_NONE, 4 },
  /* 75 */ { "ADC",   ZERO_PAGE_X, M_ZERX, M_AC,   4 }, /* Zeropage,X */
  /* 76 */ { "ROR",   ZERO_PAGE_X, M_ZERX, M_ZERX, 6 }, /* Zeropage,X */
  /* 77 */ { ".RRA",  ZERO_PAGE_X, M_ZERX, M_ZERX, 6 },

  /* 78 */ { "SEI",   IMPLIED,    M_NONE, M_FI,   2 },
  /* 79 */ { "ADC",   ABSOLUTE_Y, M_ABSY, M_AC,   4 }, /* Absolute,Y */
  /* 7a */ { ".NOOP", IMPLIED,    M_NONE, M_NONE, 2 },
  /* 7b */ { ".RRA",  ABSOLUTE_Y, M_ABSY, M_ABSY, 7 },

  /* 7c */ { ".NOOP", ABSOLUTE_X, M_NONE, M_NONE, 4 },
  /* 7d */ { "ADC",   ABSOLUTE_X, M_ABSX, M_AC,   4 },  /* Absolute,X */
  /* 7e */ { "ROR",   ABSOLUTE_X, M_ABSX, M_ABSX, 7 },  /* Absolute,X */
  /* 7f */ { ".RRA",  ABSOLUTE_X, M_ABSX, M_ABSX, 7 },

  /****  Negative  ****/

  /* 80 */ { ".NOOP", IMMEDIATE,  M_NONE, M_NONE, 2 },
  /* 81 */ { "STA",   INDIRECT_X, M_AC,   M_INDX, 6 }, /* (Indirect,X) */
  /* 82 */ { ".NOOP", IMMEDIATE,  M_NONE, M_NONE, 2 },
  /* 83 */ { ".SAX",  INDIRECT_X, M_ANXR, M_INDX, 6 },

  /* 84 */ { "STY",   ZERO_PAGE, M_YR,   M_ZERO, 3 }, /* Zeropage */
  /* 85 */ { "STA",   ZERO_PAGE, M_AC,   M_ZERO, 3 }, /* Zeropage */
  /* 86 */ { "STX",   ZERO_PAGE, M_XR,   M_ZERO, 3 }, /* Zeropage */
  /* 87 */ { ".SAX",  ZERO_PAGE, M_ANXR, M_ZERO, 3 },

  /* 88 */ { "DEY",   IMPLIED,   M_YR,   M_YR,   2 },
  /* 89 */ { ".NOOP", IMMEDIATE, M_NONE, M_NONE, 2 },
  /* 8a */ { "TXA",   IMPLIED,   M_XR,   M_AC,   2 },
  /****  ver abnormal: usually AC = AC | #$EE & XR & #$oper  ****/
  /* 8b */ { ".ANE",  IMMEDIATE, M_AXIM, M_AC,   2 },

  /* 8c */ { "STY",   ABSOLUTE, M_YR,   M_ABS, 4 }, /* Absolute */
  /* 8d */ { "STA",   ABSOLUTE, M_AC,   M_ABS, 4 }, /* Absolute */
  /* 8e */ { "STX",   ABSOLUTE, M_XR,   M_ABS, 4 }, /* Absolute */
  /* 8f */ { ".SAX",  ABSOLUTE, M_ANXR, M_ABS, 4 },

  /* 90 */ { "BCC",   RELATIVE,   M_REL,  M_NONE, 2 },
  /* 91 */ { "STA",   INDIRECT_Y, M_AC,   M_INDY, 6 }, /* (Indirect),Y */
  /* 92 */ { ".JAM",  IMPLIED,    M_NONE, M_NONE, 0 }, /* TILT relative? */
  /* 93 */ { ".SHA",  INDIRECT_Y, M_ANXR, M_STH0, 6 },

  /* 94 */ { "STY",   ZERO_PAGE_X, M_YR,   M_ZERX, 4 }, /* Zeropage,X */
  /* 95 */ { "STA",   ZERO_PAGE_X, M_AC,   M_ZERX, 4 }, /* Zeropage,X */
  /* 96 */ { "STX",   ZERO_PAGE_Y, M_XR,   M_ZERY, 4 }, /* Zeropage,Y */
  /* 97 */ { ".SAX",  ZERO_PAGE_Y, M_ANXR, M_ZERY, 4 },

  /* 98 */ { "TYA",   IMPLIED,    M_YR,   M_AC,   2 },
  /* 99 */ { "STA",   ABSOLUTE_Y, M_AC,   M_ABSY, 5 }, /* Absolute,Y */
  /* 9a */ { "TXS",   IMPLIED,    M_XR,   M_SP,   2 },
  /*** This s very mysterious comm AND ... */
  /* 9b */ { ".SHS",  ABSOLUTE_Y, M_ANXR, M_STH3, 5 },

  /* 9c */ { ".SHY",  ABSOLUTE_X, M_YR,   M_STH2, 5 },
  /* 9d */ { "STA",   ABSOLUTE_X, M_AC,   M_ABSX, 5 }, /* Absolute,X */
  /* 9e */ { ".SHX",  ABSOLUTE_Y, M_XR  , M_STH1, 5 },
  /* 9f */ { ".SHA",  ABSOLUTE_Y, M_ANXR, M_STH1, 5 },

  /* a0 */ { "LDY",   IMMEDIATE,  M_IMM,  M_YR,   2 }, /* Immediate */
  /* a1 */ { "LDA",   INDIRECT_X, M_INDX, M_AC,   6 }, /* (indirect,X) */
  /* a2 */ { "LDX",   IMMEDIATE,  M_IMM,  M_XR,   2 }, /* Immediate */
  /* a3 */ { ".LAX",  INDIRECT_X, M_INDX, M_ACXR, 6 }, /* (indirect,X) */

  /* a4 */ { "LDY",   ZERO_PAGE, M_ZERO, M_YR,   3 }, /* Zeropage */
  /* a5 */ { "LDA",   ZERO_PAGE, M_ZERO, M_AC,   3 }, /* Zeropage */
  /* a6 */ { "LDX",   ZERO_PAGE, M_ZERO, M_XR,   3 }, /* Zeropage */
  /* a7 */ { ".LAX",  ZERO_PAGE, M_ZERO, M_ACXR, 3 },

  /* a8 */ { "TAY",   IMPLIED,   M_AC,   M_YR,   2 },
  /* a9 */ { "LDA",   IMMEDIATE, M_IMM,  M_AC,   2 }, /* Immediate */
  /* aa */ { "TAX",   IMPLIED,   M_AC,   M_XR,   2 },
  /* ab */ { ".LXA",  IMMEDIATE, M_ACIM, M_ACXR, 2 }, /* LXA isn't a typo */

  /* ac */ { "LDY",   ABSOLUTE, M_ABS, M_YR,   4 }, /* Absolute */
  /* ad */ { "LDA",   ABSOLUTE, M_ABS, M_AC,   4 }, /* Absolute */
  /* ae */ { "LDX",   ABSOLUTE, M_ABS, M_XR,   4 }, /* Absolute */
  /* af */ { ".LAX",  ABSOLUTE, M_ABS, M_ACXR, 4 },

  /* b0 */ { "BCS",   RELATIVE,   M_REL,  M_NONE, 2 },
  /* b1 */ { "LDA",   INDIRECT_Y, M_INDY, M_AC,   5 }, /* (indirect),Y */
  /* b2 */ { ".JAM",  IMPLIED,    M_NONE, M_NONE, 0 }, /* TILT */
  /* b3 */ { ".LAX",  INDIRECT_Y, M_INDY, M_ACXR, 5 },

  /* b4 */ { "LDY",   ZERO_PAGE_X, M_ZERX, M_YR,   4 }, /* Zeropage,X */
  /* b5 */ { "LDA",   ZERO_PAGE_X, M_ZERX, M_AC,   4 }, /* Zeropage,X */
  /* b6 */ { "LDX",   ZERO_PAGE_Y, M_ZERY, M_XR,   4 }, /* Zeropage,Y */
  /* b7 */ { ".LAX",  ZERO_PAGE_Y, M_ZERY, M_ACXR, 4 },

  /* b8 */ { "CLV",   IMPLIED,    M_NONE, M_FV,   2 },
  /* b9 */ { "LDA",   ABSOLUTE_Y, M_ABSY, M_AC,   4 }, /* Absolute,Y */
  /* ba */ { "TSX",   IMPLIED,    M_SP,   M_XR,   2 },
  /* bb */ { ".LAS",  ABSOLUTE_Y, M_SABY, M_ACXS, 4 },

  /* bc */ { "LDY",   ABSOLUTE_X, M_ABSX, M_YR,   4 }, /* Absolute,X */
  /* bd */ { "LDA",   ABSOLUTE_X, M_ABSX, M_AC,   4 }, /* Absolute,X */
  /* be */ { "LDX",   ABSOLUTE_Y, M_ABSY, M_XR,   4 }, /* Absolute,Y */
  /* bf */ { ".LAX",  ABSOLUTE_Y, M_ABSY, M_ACXR, 4 },

  /* c0 */ { "CPY",   IMMEDIATE,  M_IMM,  M_NONE, 2 }, /* Immediate */
  /* c1 */ { "CMP",   INDIRECT_X, M_INDX, M_NONE, 6 }, /* (Indirect,X) */
  /* c2 */ { ".NOOP", IMMEDIATE,  M_NONE, M_NONE, 2 }, /* occasional TILT */
  /* c3 */ { ".DCP",  INDIRECT_X, M_INDX, M_INDX, 8 },

  /* c4 */ { "CPY",   ZERO_PAGE, M_ZERO, M_NONE, 3 }, /* Zeropage */
  /* c5 */ { "CMP",   ZERO_PAGE, M_ZERO, M_NONE, 3 }, /* Zeropage */
  /* c6 */ { "DEC",   ZERO_PAGE, M_ZERO, M_ZERO, 5 }, /* Zeropage */
  /* c7 */ { ".DCP",  ZERO_PAGE, M_ZERO, M_ZERO, 5 },

  /* c8 */ { "INY",   IMPLIED,   M_YR,  M_YR,   2 },
  /* c9 */ { "CMP",   IMMEDIATE, M_IMM, M_NONE, 2 }, /* Immediate */
  /* ca */ { "DEX",   IMPLIED,   M_XR,  M_XR,   2 },
  /* cb */ { ".SBX",  IMMEDIATE, M_IMM, M_XR,   2 },

  /* cc */ { "CPY",   ABSOLUTE, M_ABS, M_NONE, 4 }, /* Absolute */
  /* cd */ { "CMP",   ABSOLUTE, M_ABS, M_NONE, 4 }, /* Absolute */
  /* ce */ { "DEC",   ABSOLUTE, M_ABS, M_ABS,  6 }, /* Absolute */
  /* cf */ { ".DCP",  ABSOLUTE, M_ABS, M_ABS,  6 },

  /* d0 */ { "BNE",   RELATIVE,   M_REL,  M_NONE, 2 },
  /* d1 */ { "CMP",   INDIRECT_Y, M_INDY, M_NONE, 5 }, /* (Indirect),Y */
  /* d2 */ { ".JAM",  IMPLIED,    M_NONE, M_NONE, 0 }, /* TILT */
  /* d3 */ { ".DCP",  INDIRECT_Y, M_INDY, M_INDY, 8 },

  /* d4 */ { ".NOOP", ZERO_PAGE_X, M_NONE, M_NONE, 4 },
  /* d5 */ { "CMP",   ZERO_PAGE_X, M_ZERX, M_NONE, 4 }, /* Zeropage,X */
  /* d6 */ { "DEC",   ZERO_PAGE_X, M_ZERX, M_ZERX, 6 }, /* Zeropage,X */
  /* d7 */ { ".DCP",  ZERO_PAGE_X, M_ZERX, M_ZERX, 6 },

  /* d8 */ { "CLD",   IMPLIED,    M_NONE, M_FD,   2 },
  /* d9 */ { "CMP",   ABSOLUTE_Y, M_ABSY, M_NONE, 4 }, /* Absolute,Y */
  /* da */ { ".NOOP", IMPLIED,    M_NONE, M_NONE, 2 },
  /* db */ { ".DCP",  ABSOLUTE_Y, M_ABSY, M_ABSY, 7 },

  /* dc */ { ".NOOP", ABSOLUTE_X, M_NONE, M_NONE, 4 },
  /* dd */ { "CMP",   ABSOLUTE_X, M_ABSX, M_NONE, 4 }, /* Absolute,X */
  /* de */ { "DEC",   ABSOLUTE_X, M_ABSX, M_ABSX, 7 }, /* Absolute,X */
  /* df */ { ".DCP",  ABSOLUTE_X, M_ABSX, M_ABSX, 7 },

  /* e0 */ { "CPX",   IMMEDIATE,  M_IMM,  M_NONE, 2 }, /* Immediate */
  /* e1 */ { "SBC",   INDIRECT_X, M_INDX, M_AC,   6 }, /* (Indirect,X) */
  /* e2 */ { ".NOOP", IMMEDIATE,  M_NONE, M_NONE, 2 },
  /* e3 */ { ".ISB",  INDIRECT_X, M_INDX, M_INDX, 8 },

  /* e4 */ { "CPX",   ZERO_PAGE, M_ZERO, M_NONE, 3 }, /* Zeropage */
  /* e5 */ { "SBC",   ZERO_PAGE, M_ZERO, M_AC,   3 }, /* Zeropage */
  /* e6 */ { "INC",   ZERO_PAGE, M_ZERO, M_ZERO, 5 }, /* Zeropage */
  /* e7 */ { ".ISB",  ZERO_PAGE, M_ZERO, M_ZERO, 5 },

  /* e8 */ { "INX",   IMPLIED,   M_XR,   M_XR,   2 },
  /* e9 */ { "SBC",   IMMEDIATE, M_IMM,  M_AC,   2 }, /* Immediate */
  /* ea */ { "NOP",   IMPLIED,   M_NONE, M_NONE, 2 },
  /* eb */ { ".USBC", IMMEDIATE, M_IMM,  M_AC,   2 }, /* same as e9 */

  /* ec */ { "CPX",   ABSOLUTE, M_ABS, M_NONE, 4 }, /* Absolute */
  /* ed */ { "SBC",   ABSOLUTE, M_ABS, M_AC,   4 }, /* Absolute */
  /* ee */ { "INC",   ABSOLUTE, M_ABS, M_ABS,  6 }, /* Absolute */
  /* ef */ { ".ISB",  ABSOLUTE, M_ABS, M_ABS,  6 },

  /* f0 */ { "BEQ",   RELATIVE,   M_REL,  M_NONE, 2 },
  /* f1 */ { "SBC",   INDIRECT_Y, M_INDY, M_AC,   5 }, /* (Indirect),Y */
  /* f2 */ { ".JAM",  IMPLIED,    M_NONE, M_NONE, 0 }, /* TILT */
  /* f3 */ { ".ISB",  INDIRECT_Y, M_INDY, M_INDY, 8 },

  /* f4 */ { ".NOOP", ZERO_PAGE_X, M_NONE, M_NONE, 4 },
  /* f5 */ { "SBC",   ZERO_PAGE_X, M_ZERX, M_AC,   4 }, /* Zeropage,X */
  /* f6 */ { "INC",   ZERO_PAGE_X, M_ZERX, M_ZERX, 6 }, /* Zeropage,X */
  /* f7 */ { ".ISB",  ZERO_PAGE_X, M_ZERX, M_ZERX, 6 },

  /* f8 */ { "SED",   IMPLIED,    M_NONE, M_FD,   2 },
  /* f9 */ { "SBC",   ABSOLUTE_Y, M_ABSY, M_AC,   4 }, /* Absolute,Y */
  /* fa */ { ".NOOP", IMPLIED,    M_NONE, M_NONE, 2 },
  /* fb */ { ".ISB",  ABSOLUTE_Y, M_ABSY, M_ABSY, 7 },

  /* fc */ { ".NOOP", ABSOLUTE_X, M_NONE, M_NONE, 4 },
  /* fd */ { "SBC",   ABSOLUTE_X, M_ABSX, M_AC,   4 }, /* Absolute,X */
  /* fe */ { "INC",   ABSOLUTE_X, M_ABSX, M_ABSX, 7 }, /* Absolute,X */
  /* ff */ { ".ISB",  ABSOLUTE_X, M_ABSX, M_ABSX, 7 }
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
const char* DiStella::ourTIAMnemonic[62] = {
  "VSYNC", "VBLANK", "WSYNC", "RSYNC", "NUSIZ0", "NUSIZ1", "COLUP0", "COLUP1",
  "COLUPF", "COLUBK", "CTRLPF", "REFP0", "REFP1", "PF0", "PF1", "PF2", "RESP0",
  "RESP1", "RESM0", "RESM1", "RESBL", "AUDC0", "AUDC1", "AUDF0", "AUDF1",
  "AUDV0", "AUDV1", "GRP0", "GRP1", "ENAM0", "ENAM1", "ENABL", "HMP0", "HMP1",
  "HMM0", "HMM1", "HMBL", "VDELP0", "VDELP1", "VDELBL", "RESMP0", "RESMP1",
  "HMOVE", "HMCLR", "CXCLR", "$2D", "$2E", "$2F", "CXM0P", "CXM1P", "CXP0FB",
  "CXP1FB", "CXM0FB", "CXM1FB", "CXBLPF", "CXPPMM", "INPT0", "INPT1", "INPT2",
  "INPT3", "INPT4", "INPT5"
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
const char* DiStella::ourIOMnemonic[24] = {
  "SWCHA", "SWACNT", "SWCHB", "SWBCNT", "INTIM", "$0285", "$0286", "$0287",
  "$0288", "$0289", "$028A", "$028B", "$028C", "$028D", "$028E", "$028F",
  "$0290", "$0291", "$0292", "$0293", "TIM1T", "TIM8T", "TIM64T", "T1024T"
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
const int DiStella::ourCLength[14] = {
  1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 2, 2, 2, 0
};
