#include "mbed.h"

Serial pc(SERIAL_TX, SERIAL_RX);
SPI lepton_spi(SPI_MOSI, SPI_MISO, SPI_SCK);
I2C i2c(I2C_SDA, I2C_SCL);
DigitalOut spi_cs(SPI_CS);
DigitalOut beep(D2);
#define VOSPI_FRAME_SIZE (164) 
uint8_t lepton_frame_packet[VOSPI_FRAME_SIZE]; 
int lepton_image[80][80];

#define ADDRESS  (0x54)

#define COMMANDID_REG (0x04)
#define DATALEN_REG (0x06)
#define DATA0 (0x08)


#define AGC (0x01)
#define SYS (0x02)
#define VID (0x03)
#define OEM (0x08)

#define GET (0x00)
#define SET (0x01)
#define RUN (0x02)



int print_image_binary_state =-1;
int print_image_binary_i;
int print_image_binary_j;


static void print_image_binary_background(void)
{
    if( print_image_binary_state == -1)
    {
        return;
    }
    else if( print_image_binary_state == 0)
    {
        pc.putc(0xDE);
        print_image_binary_state++;
    } 
    else if( print_image_binary_state == 1)
    {
        pc.putc(0xAD);
        print_image_binary_state++;
    } 
    else if( print_image_binary_state == 2)
    {
        pc.putc(0xBE);
        print_image_binary_state++;
    } 
    else if( print_image_binary_state == 3)
    {
        pc.putc(0xEF);
        print_image_binary_state++;
        print_image_binary_i = 0;
        print_image_binary_j = 0;
    } 
    else if( print_image_binary_state == 4)
    {
        while(pc.writeable() == 0);
        pc.putc((lepton_image[print_image_binary_i][print_image_binary_j]>>8)&0xff);
        while(pc.writeable() == 0);
        pc.putc(lepton_image[print_image_binary_i][print_image_binary_j]&0xff);

        print_image_binary_j++;
        if(print_image_binary_j>=80)
        {
            print_image_binary_j=0;
            print_image_binary_i++;
            if(print_image_binary_i>=60)
            {
                print_image_binary_state = -1;
                //frame_c++;
            }
        }
    }

}



int lost_frame_counter = 0;
int last_frame_number;
int frame_complete = 0;
int start_image = 0;
int need_resync = 0;
int last_crc = 0;
int new_frame = 0;
int frame_counter = 0;
int pic_c = 0;
void transfer(void)
{
    int i;
    int frame_number;

    spi_cs = 0;
    
    for(i=0;i<VOSPI_FRAME_SIZE;i++)
    {
        lepton_frame_packet[i] = lepton_spi.write(0x00);
    }
    
    spi_cs = 1;


    if(((lepton_frame_packet[0]&0x0f) != 0x0f))
    {
        if(lepton_frame_packet[1] == 0  )
        {
            if(last_crc != (lepton_frame_packet[2]<<8 | lepton_frame_packet[3]))
            {
                new_frame = 1;
            }
            last_crc = lepton_frame_packet[2]<<8 | lepton_frame_packet[3];
        }
        frame_number = lepton_frame_packet[1];
        //pc.printf("took %d frames", frame_number);
        if(frame_number < 60 )
        {
            lost_frame_counter = 0;
            if(print_image_binary_state == -1)
            {
                for(i=0;i<80;i++)
                {
                    lepton_image[frame_number][i] = (lepton_frame_packet[2*i+4] << 8 | lepton_frame_packet[2*i+5]);
                }
                
            }
        }
        else
        {
            lost_frame_counter++;
        }
        if( frame_number == 59)
        {
            frame_complete = 1;
            last_frame_number = 0;
            
        }
    }
    //else
    //{
        //if(last_frame_number ==0)
        //{
        //}
    //}

    //lost_frame_counter++;
    if(lost_frame_counter>100)
    {
        need_resync = 1;
        lost_frame_counter = 0;

    }

    if(need_resync)
    {
        wait_ms(185);
        need_resync = 0; 
    }


    if(frame_complete)
    {
        if(new_frame)
        {
            frame_counter++;
            
            {
                if(frame_counter%18 ==0)
                {
                    print_image_binary_state = 0;
                   //pic_c++;
                   //pc.printf("%d, ",timer.read_ms());
                    
                }
            }
            new_frame = 0;
        }
        frame_complete = 0;
    }
}

void lepton_command(unsigned int moduleID, unsigned int commandID, unsigned int command)
{
  int error;
  char data_write[4];
  
  // Command Register is a 16-bit register located at Register Address 0x0004
  data_write[0] = (0x00);
  data_write[1] = (0x04);

  data_write[2] = (moduleID & 0x0f);
  data_write[3] = (((commandID << 2 ) & 0xfc) | (command & 0x3));

  error = i2c.write(ADDRESS,data_write,4,0);    
  
  if (error != 0)
  {
    pc.printf("error=");
    pc.printf("%d",error);
  }
}

void set_reg(unsigned int reg)
{
  int error;
  char data_write[2];
  data_write[0] = (reg >> 8 & 0xff);
  data_write[1] = (reg & 0xff);            

  error = i2c.write(ADDRESS,data_write,2,0);
  
  if (error != 0)
  {
    pc.printf("error=");
    pc.printf("%d",error);
  }
}

//Status reg 15:8 Error Code  7:3 Reserved 2:Boot Status 1:Boot Mode 0:busy

int read_reg(unsigned int reg)
{
  int reading = 0;
  char read_data[2];
  set_reg(reg);
  i2c.read(ADDRESS,read_data,2,0);
  
  reading = read_data[0];  // receive high byte (overwrites previous reading)
  reading = reading << 8;    // shift high byte to be high 8 bits
  reading |= read_data[1]; // receive low byte as lower 8 bits
  
  return reading;
}

int read_data()
{
  int i;
  int data;
  int payload_length;
  int sum = 0;
  char data_read[2];

  while (read_reg(0x2) & 0x01)
  {
    pc.printf("busy");
  }

  payload_length = read_reg(0x6);
  
  for (i = 0; i < (payload_length / 2); i++)
  {
    i2c.read(ADDRESS,data_read,2,1);
    data = data_read[0] << 8;
    data |= data_read[1];
    sum = sum+data;
  }
  i2c.stop();
  return sum;
}

void metric_enable()
{
  int error;
  char data_write[4];
  //16bit data_reg address
  data_write[0] = (0x00);
  data_write[1] = (DATA0);
  //16bit command equivalent to SDK LEP_GetAgcEnableState()
  data_write[2] = (0x00);
  data_write[3] = (0x01);
  error = i2c.write(ADDRESS,data_write,4,0);
  if (error != 0)
  {
    pc.printf("error=");
    pc.printf("%d",error);
  }
  //16bit data_len address
  data_write[0] = (0x00);
  data_write[1] = (DATALEN_REG);
  //16bit value for number of bytes in data_regs (not number of regs)
  data_write[2] = (0x00);
  data_write[3] = (0x02);
  error = i2c.write(ADDRESS,data_write,4,0);
 if (error != 0)
  {
    pc.printf("error=");
    pc.printf("%d",error);
  }
  //16bit command_reg address
  data_write[0] = (0x00);
  data_write[1] = (COMMANDID_REG);
  //16bit module id VID enable focus metric calculation
  data_write[2] = (0x03);
  data_write[3] = (0x0D);
  error = i2c.write(ADDRESS,data_write,4,0);
  if (error != 0)
  {
    pc.printf("error=");
    pc.printf("%d",error);
  }
}

void tresh()
{
  int error;
  char data_write[4];
  //16bit data_reg address
  data_write[0] = (0x00);
  data_write[1] = (DATA0);
  //16bit command - treshold 
  data_write[2] = (0x75);
  data_write[3] = (0x30);
  error = i2c.write(ADDRESS,data_write,4,0);
  if (error != 0)
  {
    pc.printf("error=");
    pc.printf("%d",error);
  }
  //16bit data_len address
  data_write[0] = (0x00);
  data_write[1] = (DATALEN_REG);
  //16bit value for number of bytes in data_regs (not number of regs)
  data_write[2] = (0x00);
  data_write[3] = (0x02);
  error = i2c.write(ADDRESS,data_write,4,0);
  if (error != 0)
  {
    pc.printf("error=");
    pc.printf("%d",error);
  }
  //16bit command_reg address
  data_write[0] = (0x00);
  data_write[1] = (COMMANDID_REG);
  //16bit module id  VID set Focus metric treshold
  data_write[2] = (0x03);
  data_write[3] = (0x15);
  error = i2c.write(ADDRESS,data_write,4,0);
 if (error != 0)
  {
    pc.printf("error=");
    pc.printf("%d",error);
  }
}

int main() 
{
    beep = 0;
    int i = 0;
    pc.baud(460800);
    lepton_spi.format(8,3);
    lepton_spi.frequency(20000000);
    
    wait_ms(2000);
    spi_cs = 1;
    spi_cs = 0;
    spi_cs = 1;

    read_reg(0x2);

    int j;
    metric_enable();
    tresh();
    while(1) 
    {
        //transfer();
        //print_image_binary_background();
        
        lepton_command(VID, 0x18 >> 2 , GET);
        j = read_data();
        lepton_command(VID, 0x18 >> 2 , GET);
        i = read_data();
        if((i-j) > 300)
        {
            
            pc.printf("%d , ",(i-j));
            beep = 1;
            wait(0.5);
            beep = 0;    
         }
        
    }
}