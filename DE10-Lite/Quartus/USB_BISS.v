module USB_BISS(
	//////////// CLOCK //////////
	input 		          		ADC_CLK_10,
	input 		          		MAX10_CLK1_50,
	input 		          		MAX10_CLK2_50,
	//////////// SEG7 //////////
	output		     [7:0]		HEX0,
	output		     [7:0]		HEX1,
	output		     [7:0]		HEX2,
	output		     [7:0]		HEX3,
	output		     [7:0]		HEX4,
	output		     [7:0]		HEX5,
	//////////// KEY //////////
	input 		     [1:0]		KEY,
	//////////// LED //////////
	output		     [9:0]		LEDR,
	//////////// SW //////////
	input 		     [9:0]		SW,
	//////////// Arduino //////////
	inout 		    [15:0]		ARDUINO_IO,
	inout 		          		ARDUINO_RESET_N,
	//////////// GPIO, GPIO connect to GPIO Default //////////
	inout 		    [35:0]		AGPIO
);

wire [15:0]LOG;
assign AGPIO[15:0] = LOG;
wire clk = MAX10_CLK1_50;
assign LOG[15:0]={ARDUINO_IO[10], ARDUINO_IO[13:11], MA_sync};
//=====================================================================================================

reg [2:0]clk_50_div;
always @(posedge clk)clk_50_div <= clk_50_div + 1;

wire RX_IN = ARDUINO_IO[12];
assign ARDUINO_IO[13] = UART_MODE_EN? SLO : TX;
assign ARDUINO_IO[10] = UART_MODE_EN? RX_IN : MA;
wire SLO = ARDUINO_IO[11];
assign ARDUINO_IO[9] = POWER_CTRL & KEY[0];

//===========================================================================================
assign LEDR[9:0] = {NO_ACK, ~SCD_data[7:6], ~CRC6_OK};

assign HEX5 = {1'b1,HEX[7][6:0]};
assign HEX4 = {1'b1,HEX[6][6:0]};
assign HEX3 = {1'b0,HEX[5][6:0]};
assign HEX2 = {1'b1,HEX[4][6:0]};
assign HEX1 = {1'b0,HEX[3][6:0]};
assign HEX0 = {1'b1,HEX[2][6:0]};

wire [6:0] HEX[7:0];
reg [23:0]Slow_angle;
reg [20:0]slow_clk_div;
always @(posedge clk)slow_clk_div <= slow_clk_div + 1;
always @(posedge slow_clk_div[20]) Slow_angle <= SCD_data[31:8];
bin2deghex b2dh_1(.clk(MAX10_CLK1_50),
.deg(Slow_angle),
.HEX7(HEX[7]),
.HEX6(HEX[6]),
.HEX5(HEX[5]),
.HEX4(HEX[4]),
.HEX3(HEX[3]),
.HEX2(HEX[2]),
.HEX1(HEX[1]),
.HEX0(HEX[0])
);
//===========================================================================================


//===========================================================================================
//BISS TEMPLATE
//===========================================================================================
reg NO_ACK, CDS, CDM, MA, MA_sync, MA_sync_start,SCD_start,CRC6_OK, sen_MA, BISS_read_end, SCD_finish;

reg [5:0]BISS_adr_cou;


reg [3:0] clk_div;
reg [8:0]CDM_cou;
reg [15:0]CDM_shift;

reg [7:0] BISS_read_data;
wire [3:0]CRC4;
reg [10:0]CRC4_in;

reg [13:0]MA_cou,MA_sync_cou;
reg [8:0]SCD_cou;
reg [31:0]SCD_shift,SCD_data;

reg [5:0]ACK_cou,CRC6;

wire [7:0] SCD_size = 32;

wire MA_clk = clk10M;

CRC4_calc crc1(.data(CRC4_in[10:0]),.CRC(CRC4));

always @(posedge MA_sync_start or negedge BISS_start)begin
	if(~BISS_start)begin 
		CDM_cou <= 0;
		BISS_read_end <= 0;
	end
	else begin		
		case(CDM_cou)
			0:	begin				
				CDM_cou <= CDM_cou + 1;
				CDM <= 1;
				CRC4_in <= {1'b1,3'b0,BISS_adr};		
			end
			1: begin
				CDM_cou <= CDM_cou + 1;
				CDM <= 1;
				CDM_shift[15:0] <= {3'b0,BISS_adr,CRC4,RnW,~RnW};
				CRC4_in <={3'b0,BISS_data_size};
				BISS_adr_cou <= BISS_data_size[5:0];
			end
			18: begin		
				CDM_cou <= CDM_cou + 1;
				CDM <= 1;
				if (~RnW)				
					CDM_shift[15:0] <= {BISS_data_size,CRC4,4'b0};		
			end
			32: begin
				if(RnW)begin
					
					if(BISS_adr_cou)begin
						BISS_adr_cou <= BISS_adr_cou - 1;
						CDM_cou <= 19;
						CDM <= 1;						
					end
					else begin
						CDM <= 0;
					end
				end
				else begin
					CDM <= 0;
				end				
			end
			default: begin
				BISS_read_end <= CDM_cou == 30;
				CDM_cou <= CDM_cou + 1;
				if(RnW & (CDM_cou > 17))begin
					CDM <= 0;
					CDM_shift[15:0] <= {CDM_shift[14:0],CDS};
					if(CDM_cou == 29)
						BISS_read_data <= CDM_shift[10:0];
				end
				else begin
					{CDM,CDM_shift[15:0]} <= {CDM_shift[15:0],1'b0};	
				end
			end
		endcase
	end		
end 

always @(posedge MA_clk)begin
	if((MA_cou > 0) | SLO) begin
		if(MA_sync_start)begin
			if((MA_sync_cou[13:3] > ACK_cou + SCD_size + 11) & SLO)begin
				MA_cou <= 0;
				NO_ACK <= 0;
			end
			else if (SW[0])
				MA_cou <= MA_cou + 1;		
		end
		else if(MA_cou[13:3] > SCD_size + 64)begin
			MA_cou <= 0;
			NO_ACK <= 1;
		end
		else if (SW[0])
			MA_cou <= MA_cou + 1;	
	end
	if(CDM)
		MA <= (~MA_cou[2]) & (MA_cou[13:3] < (ACK_cou + SCD_size + 5));
	else
		MA <= (~MA_cou[2]) | (MA_cou[13:3] > (ACK_cou + SCD_size + 3));
	if(MA_cou[13:3] < 2) begin
		MA_sync <= ~MA_cou[2];		
		MA_sync_start <= 0;
		MA_sync_cou <= 6;
		ACK_cou <= 0;
		SCD_start <= 0;
		SCD_cou <= 0;
	end	
	else begin
		if(MA_sync_start)begin
			MA_sync_cou <= MA_sync_cou + 1;
			MA_sync <= MA_sync_cou[2] | (MA_sync_cou[13:3] > (ACK_cou + SCD_size + 2));
			if (MA_sync_cou[2:0] == 0)begin
				if(SCD_start)begin
					if(SCD_cou < (SCD_size + 2))begin
						SCD_cou <= SCD_cou + 1;
						if(SCD_cou < 33)begin
							if(SCD_cou == 0)begin
								CRC6 <= 0;
								CDS <= SLO;
							end
							else begin								
								SCD_shift[32 - SCD_cou] <= SLO;	
								if(SCD_cou < 27)			
									CRC6[5:0] <= {CRC6[4:1], CRC6[5] ^ CRC6[0] ^ SLO, CRC6[5] ^ SLO};
							end 				
						end
						else begin								
							if(CRC6 == (~SCD_shift[5:0]))begin
								SCD_finish <= 1;
								SCD_data <= SCD_shift;
								CRC6_OK <= 1;
							end
							else
								CRC6_OK <= 0;
						end
					end
				end
				else begin
					if(SLO)begin
						SCD_finish <= 0;
						SCD_start <= 1;
					end
					else
						ACK_cou <= ACK_cou + 1;
				end
			end
			else if((MA_sync_cou[2:0] == 6) & SCD_start) 
				sen_MA <= (SCD_cou > SCD_size) | (SCD_cou < 1);
		end
		else begin
			MA_sync_start <= ~SLO;
			MA_sync <= ~SLO;
		end 		
	end	
end

//===========================================================================================
wire [6:0] BISS_adr;
wire [7:0] BISS_data_size;
pll3m7 pll_1(.inclk0(MAX10_CLK1_50),.c0(clk7M4),.c1(clk10M),.c2(clk12M));
UART uart_1(
.clk12M(clk12M),
.clk7M4(clk7M4),
.RX_IN(RX_IN),
.TX(TX),
.POWER_CTRL(POWER_CTRL),
.UART_MODE_EN(UART_MODE_EN),
.TX_start(TX_start),
.DATA(SCD_data[31:8]),
.data_sync(SCD_finish),
.BISS_start(BISS_start),
.BISS_adr(BISS_adr),
.BISS_data_size(BISS_data_size),
.RnW(RnW),
.BISS_read_data(BISS_read_data),
.BISS_read_end(BISS_read_end)
);


endmodule
//=====================================================================================================
//BISS MODULE


//=====================================================================================================
//UART MODULE

module UART(input clk12M,input clk7M4, input RX_IN, output reg TX,output reg POWER_CTRL,
				input [23:0] DATA, input data_sync, output reg UART_MODE_EN,output reg TX_start, 
				output reg RnW, input [7:0] BISS_read_data, output reg [6:0] BISS_adr,
				output reg [7:0] BISS_data_size, output reg BISS_start, input BISS_read_end);
initial begin
	UART_CLK_7M4_n3M7<=0;
	TX_start<=0;
	tx_clk_cou<=0;
	POWER_CTRL<=1;
	UART_MODE_EN<=0;
	TX<=1;
	TX_cou<=0;
	BISS_start <= 1;
end
//=====================================================================================================

reg clk3M7;
always @(posedge clk7M4)clk3M7<=~clk3M7;
//=====================================================================================================
//UART RX BEGIN
reg [7:0] UART_RX_SHIFT,RX_DATA[7:0],BISS_RX[63:0],uart_rx_cou,rx_len,RX_HEX_CRC,RX_OUT,TX_CMD,TX_CRC,TX_buf,data_cou,buf_cou_m;
reg [23:0]DATA_BUF[255:0];
always @(negedge data_sync)begin
	data_cou <= data_cou + 1;
	DATA_BUF[data_cou] <= DATA;
end
reg uart_rx_busy,uart_rx_end_ok,RX_filt_d,TX_busy,TX_start_d,UART_CLK_7M4_n3M7,TX_en;
reg [6:0]tx_clk_cou,BISS_read_cou;
reg [23:0]TX_cou;
wire UART_CLK=UART_CLK_7M4_n3M7?clk7M4:clk3M7;
wire [2:0]len_m1;
assign len_m1=rx_len-1;
mfilt Mfilt_rx(.clk(UART_CLK),.in(RX_IN),.out(RX_filt));
reg [3:0]buf_cou_l;
parameter tx_size = 255; 
wire [7:0]dif_cou = data_cou - buf_cou_m;
reg [15:0]TX_size,packet_cnt; 
always @(posedge BISS_read_end or negedge BISS_start)begin
	if(~BISS_start)begin
		BISS_read_cou <= 0;
	end
	else begin
		BISS_RX[BISS_read_cou] <= BISS_read_data;
		BISS_read_cou <= BISS_read_cou + 1;
	end
end
//=====================================================================================================
//Transmit task
always @(negedge UART_CLK or negedge TX_start)begin	
	if(~TX_start)	begin
		if((TX_CMD < 16) | (TX_CMD == 147)) begin
			TX_cou <= 5;
			TX_en <= 1;
		end
		else if(TX_CMD == 145) begin
			TX_cou <= tx_size + 5;
			packet_cnt <= TX_size;
			TX_en <= 1;			
		end		
		else if(TX_CMD == 146) begin
			TX_cou <= {2'b0, BISS_data_size[5:0]} + 6;
			TX_en <= 0;
		end
		else if(TX_CMD == 148) begin
			TX_cou <= 8;
			TX_en <= 1;
		end
	end
	else if (TX_cou > 0) begin
		if(tx_clk_cou>78) begin					
			tx_clk_cou<=0;				
			if( (TX_cou == 1) & (TX_CMD == 145) & (packet_cnt > 0)) begin
				if ((packet_cnt < TX_size) & (dif_cou < 250))
					TX_en <= 0;	
				TX_cou <= tx_size + 5;	
				packet_cnt <= packet_cnt - 1;					
			end
			else				
				TX_cou <= TX_cou - 1;
		end
		else if(TX_en)
			tx_clk_cou <= tx_clk_cou + 1;	
		else if((dif_cou > 250) & (TX_CMD == 145))
			TX_en <= 1;	
		else if((TX_CMD == 146) & (BISS_read_cou == BISS_TX_size))
			TX_en <= 1;	
	end
end
//=====================================================================================================
wire [6:0]BISS_TX_size;
reg [23:0]BISS_data;
assign BISS_TX_size = {1'b0,BISS_data_size[5:0]} + 1;
always @(posedge tx_clk_cou[2]) begin
	if(tx_clk_cou[6:3]==0) begin
		TX <= 0;			
		if((TX_CMD < 16) | (TX_CMD == 147)) begin
			if(TX_cou>2)
				TX_buf<=RX_DATA[5-TX_cou];
			else 
				if (TX_cou==2)
					TX_buf<=RX_DATA[5-TX_cou]+16;
				else			
					TX_buf<=RX_DATA[5-TX_cou]-16;
		end
		else if (TX_CMD == 148) begin
			case(TX_cou)
				8:begin
					BISS_data <= DATA[23:0];
					TX_buf <= 3;
					TX_CRC <= 256 - 3;
				end
				7:begin
					TX_buf <= 0;
				end
				6:begin
					TX_buf <= 0;
				end
				5:begin
					TX_buf <= TX_CMD;
					TX_CRC <= TX_CRC - TX_CMD;
				end
				4:begin
					TX_buf <= BISS_data[23:16];
					TX_CRC <= TX_CRC - BISS_data[23:16];
				end
				3:begin
					TX_buf <= BISS_data[15:8];
					TX_CRC <= TX_CRC - BISS_data[15:8];
				end
				2:begin
					TX_buf <= BISS_data[7:0];
					TX_CRC <= TX_CRC - BISS_data[7:0];
				end
				1:begin
					TX_buf <= TX_CRC;
				end
			endcase
		end
		else if (TX_CMD == 146) begin
			if(TX_cou > BISS_TX_size + 1)begin
				case(TX_cou - BISS_TX_size)
					5:begin
						TX_buf <= BISS_TX_size;
						TX_CRC <= 256 - BISS_TX_size;
					end
					4:begin
						TX_buf <= RX_DATA[1];
						TX_CRC <= TX_CRC - RX_DATA[1];
					end
					3:begin
						TX_buf <= RX_DATA[2];
						TX_CRC <= TX_CRC - RX_DATA[2];
					end
					2:begin
						TX_buf <= RX_DATA[3] + 16;
						TX_CRC <= TX_CRC - RX_DATA[3] - 16;
					end
				endcase
			end
			else if(TX_cou > 1)begin
				TX_buf <= BISS_RX[BISS_TX_size + 1 - TX_cou];
				TX_CRC <= TX_CRC - BISS_RX[BISS_TX_size + 1 - TX_cou];				
			end
			else
				TX_buf <= TX_CRC;					
		end
		else if(TX_CMD == 145) begin
			if(TX_cou > tx_size + 1) begin
				case(TX_cou)
					tx_size + 5 : begin
						TX_buf <= tx_size;
						TX_CRC <= 256 - tx_size;
					end
					tx_size + 4 : begin
						TX_buf <= packet_cnt[15:8];
						TX_CRC <= TX_CRC - packet_cnt[15:8];
					end
					tx_size + 3:begin
						TX_buf <= packet_cnt[7:0];
						TX_CRC <= TX_CRC - packet_cnt[7:0];						
					end
					tx_size + 2:begin
						TX_buf <= 145;
						TX_CRC <= TX_CRC - 145;
						if(packet_cnt == TX_size)
							buf_cou_m <= data_cou + 2;
							
						buf_cou_l <= 0;
					end
				endcase
			end
			else begin
				if (TX_cou > 1) begin
					if (buf_cou_l == 2)begin
						buf_cou_l <= 0;
						buf_cou_m <= buf_cou_m + 1;
					end
					else
						buf_cou_l <= buf_cou_l + 1;
						
					case(buf_cou_l)
						0:begin
							TX_buf<=DATA_BUF[buf_cou_m][7:0];
							TX_CRC<=TX_CRC-DATA_BUF[buf_cou_m][7:0];
						end
						1:begin
							TX_buf<=DATA_BUF[buf_cou_m][15:8];
							TX_CRC<=TX_CRC-DATA_BUF[buf_cou_m][15:8];
						end
						2:begin
							TX_buf<=DATA_BUF[buf_cou_m][23:16];
							TX_CRC<=TX_CRC-DATA_BUF[buf_cou_m][23:16];
						end						
					endcase
				end
				else 
					TX_buf<=TX_CRC;			
			end
		end
	end
	else 
		if (tx_clk_cou[6:3]>8)
			TX <= 1;
		else 
			TX <= TX_buf[tx_clk_cou[6:3]-1];
end

//=====================================================================================================
//Recieve task
always @(posedge UART_CLK)begin	
	RX_filt_d<=RX_filt;	
	if (RX_filt_d&(~RX_filt)&(~uart_rx_busy)) begin
		if (uart_rx_cou==255)begin
			RX_HEX_CRC <= 0;
			rx_len <= 1;
			rx_command_reset_task();
		end
		else rx_len<=rx_len+1;		
		uart_rx_busy<=1;
		uart_rx_cou<=0;
	end 
	else begin
		if((uart_rx_cou==252)&(rx_len==5)&(RX_HEX_CRC==0)) begin
			if(RX_DATA[0]==0) begin
				case(RX_DATA[3])
					128:	rx_command_task();
					129:	begin
						TX_CMD <= RX_DATA[3] + 16;
						TX_size<={RX_DATA[1][7:0],RX_DATA[2][7:0]};						
					end
					130: begin
						TX_CMD <= RX_DATA[3] + 16;
						BISS_adr[6:0] <= RX_DATA[1][6:0];
						BISS_data_size[7:0] <= RX_DATA[2][7:0]; 
						BISS_start <= 1;
						RnW <= 1;
					end
					131: 	begin
						TX_CMD <= RX_DATA[3] + 16;
						BISS_adr[6:0] <= RX_DATA[1][6:0];
						BISS_data_size[7:0] <= RX_DATA[2][7:0]; 
						BISS_start <= 1;
						RnW <= 0;
					end
					132: 	begin
						TX_CMD <= RX_DATA[3] + 16;
					end
				endcase
			end
		end			
		if((uart_rx_cou==254)&(rx_len==5)&(RX_HEX_CRC==0)&RX_DATA[3][7])begin
			TX_start <= 1;
		end
		if (uart_rx_cou<255)
			uart_rx_cou<=uart_rx_cou+1;
		
		uart_rx_busy<=uart_rx_cou<76;
		UART_RX_SHIFT<=(uart_rx_cou[2:0]==4)?{RX_filt,UART_RX_SHIFT[7:1]}:UART_RX_SHIFT;
		if (uart_rx_cou==75)begin
			uart_rx_end_ok<=RX_filt;
			RX_HEX_CRC<=RX_HEX_CRC-UART_RX_SHIFT;
			if(rx_len<9)RX_DATA[len_m1][7:0]<=UART_RX_SHIFT[7:0];			
		end	
	end	
end
//=====================================================================================================
task rx_command_task;		
	case(RX_DATA[2])//must be lower 253
		0:begin//reset task
			TX_CMD <= 0;
		end
		// Set 460800 baud
		9:begin
			TX_CMD<=RX_DATA[2];
			UART_CLK_7M4_n3M7<=0;		
		end
		// Set 1500000 baud
		15:begin
			TX_CMD<=RX_DATA[2];
			UART_CLK_7M4_n3M7<=1;		
		end
		11:begin
			TX_CMD<=RX_DATA[2];
			POWER_CTRL<=0;
		end
		12:begin
			TX_CMD<=RX_DATA[2];
			POWER_CTRL<=1;
		end
		13:begin
			TX_CMD<=RX_DATA[2];
			UART_MODE_EN<=0;
		end
		14:begin
			TX_CMD<=RX_DATA[2];
			UART_MODE_EN<=1;		
		end
		default:
			TX_CMD<=255;
	endcase		
endtask
//=====================================================================================================
task rx_command_reset_task;
	TX_start <= 0;
	BISS_start <= 0;
endtask
//UART RX END
//=====================================================================================================
endmodule

module bin2deghex(input clk,input [23:0]deg, output [6:0] HEX0, output [6:0] HEX1, output [6:0] HEX2, output [6:0] HEX3, output [6:0] HEX4, output [6:0] HEX5, output [6:0] HEX6, output [6:0] HEX7);
	reg [33:0] D360, D60360, D6060360;
	reg [4:0] D0, D1, D2, D3, D4, D5, D6, D7;
	always @(clk)begin
		D6060360 = ((deg * 360 - (D360 << 24)) * 60 - (D60360 << 24)) * 600 >> 24;
		D60360 = (deg * 360 - (D360 << 24)) * 60 >> 24;
		D360 = deg * 360 >> 24;
		D0 = D360 / 100;
		D1 = (D360 % 100) / 10;
		D2 = D360 % 10;
		D3 = D60360 / 10;
		D4 = D60360 % 10;
		D5 = D6060360 / 100;
		D6 = (D6060360 % 100) / 10;
		D7 = D6060360 % 10;
	end	
	d2h u0(.d(D0 == 0? 15 : D0),.h(HEX7));
	d2h u1(.d((D1 + D0) == 0? 15 : D1),.h(HEX6));
d2h u2(.d(D2),.h(HEX5));
d2h u3(.d(D3),.h(HEX4));
d2h u4(.d(D4),.h(HEX3));
d2h u5(.d(D5),.h(HEX2));
d2h u6(.d(D6),.h(HEX1));
d2h u7(.d(D7),.h(HEX0));
endmodule

module d2h(input [3:0]d, output wire [6:0] h);
	assign h[6:0] = d[3] ? (d[2] ? (d[1] ? (d[0] ? (7'b1111111)	: (7'b0000110)) : (d[0] ? (7'b0100001) : (7'b1000110)))
										  : (d[1] ? (d[0] ? (7'b0000011) : (7'b0001000)) : (d[0] ? (7'b0010000) : (7'b0000000))))
								: (d[2] ? (d[1] ? (d[0] ? (7'b1111000) : (7'b0000010)) : (d[0] ? (7'b0010010) : (7'b0011001)))
										  : (d[1] ? (d[0] ? (7'b0110000) : (7'b0100100)) : (d[0] ? (7'b1111001) : (7'b1000000))));
endmodule 

module mfilt(input in,input clk,output out);
	reg [2:0]b;
	always @(posedge clk)begin
	b[0] <= in;
	b[1] <= b[0];
	b[2] <= b[1];	
	end
	assign out = (b[0] & b[1]) | (b[0] & b[2]) | (b[1] & b[2]);
endmodule

module CRC4_table(input [3:0]in,output reg[3:0] out);
	always @* 
		case(in)
			4'h0: out = 4'h0;
			4'h1: out = 4'h3;
			4'h2: out = 4'h6;
			4'h3: out = 4'h5;
			4'h4: out = 4'hc;
			4'h5: out = 4'hf;
			4'h6: out = 4'ha;
			4'h7: out = 4'h9;
			4'h8: out = 4'hb;
			4'h9: out = 4'h8;
			4'ha: out = 4'hd;
			4'hb: out = 4'he;
			4'hc: out = 4'h7;
			4'hd: out = 4'h4;
			4'he: out = 4'h1;
			4'hf: out = 4'h2;
		endcase
endmodule

module CRC4_calc(input [10:0] data, output [3:0] CRC);
	//(0xF - tableCRC4[(adr & 0xF) 	^ tableCRC4[((adr >> 4) & 0xF) ^ tableCRC4[(adr >> 8)& 0xF]])
	wire [11:0] tmp;
	
	CRC4_table tb1(.in({1'b0,data[10:8]}),.out(tmp[11:8]));
	CRC4_table tb2(.in(data[7:4] ^ tmp[11:8]),.out(tmp[7:4]));
	CRC4_table tb3(.in(data[3:0] ^ tmp[7:4]),.out(tmp[3:0]));
	assign CRC [3:0] = ~ tmp[3:0];
endmodule

