// Shrike Sentinel FPGA top-level design
// Copyright (c) 2026 RevX5
// License: CERN-OHL-1.2
// Source: https://github.com/RevX5/Shrike-Sentinal

(* top *) module shrike_sentinel_events(
    (* iopad_external_pin, clkbuf_inhibit *) input clk,
    (* iopad_external_pin *) input START,

    (* iopad_external_pin *) input spi_ss_n,
    (* iopad_external_pin *) input spi_sck,
    (* iopad_external_pin *) input spi_mosi,
    (* iopad_external_pin *) output spi_miso,
    (* iopad_external_pin *) output spi_miso_en,
    	
    (* iopad_external_pin *) output LED,
    (* iopad_external_pin *) output LED_en,

    (* iopad_external_pin *) output EVENT_OUT,
    (* iopad_external_pin *) output EVENT_OE,

    (* iopad_external_pin *) output clk_en
);

    // 50 MHz nominal FPGA clock
    reg [31:0] led_counter = 0;
    reg led_status = 0;
	
    reg start_meta = 0;
    reg start_sync = 0;
    reg start_prev = 0;

    // Event burst generator
    reg [31:0] event_counter = 0;
    reg [31:0] pulses_sent = 0;
    
	reg [3:0] por_count = 4'hF;

	always @(posedge clk) begin
    		if (por_count != 0)
        por_count <= por_count - 1'b1;
	end

	wire por_resetn = (por_count == 0);

	wire [7:0] spi_rx_data;
	wire spi_rx_valid;
	wire [7:0] spi_tx_data;

	reg spi_rx_valid_d = 1'b0;
	reg [1:0] read_sel = 2'd0;
	reg [31:0] count_snapshot = 32'd0;

	spi_target #(
    		.CPOL(1'b0),
    		.CPHA(1'b0),
    		.WIDTH(8),
    		.LSB(1'b0)
	) u_spi_target (
    		.i_clk(clk),
    		.i_rst_n(por_resetn),
    		.i_enable(1'b1),
    		.i_ss_n(spi_ss_n),
    		.i_sck(spi_sck),
    		.i_mosi(spi_mosi),
    		.o_miso(spi_miso),
    		.o_miso_oe(spi_miso_en),
    		.o_rx_data(spi_rx_data),
    		.o_rx_data_valid(spi_rx_valid),
    		.i_tx_data(spi_tx_data),
    		.o_tx_data_hold()
	);
	
	reg [7:0] spi_tx_mux;

	always @* begin
  	  	case (read_sel)
       	 	2'd0: spi_tx_mux = count_snapshot[7:0];
        		2'd1: spi_tx_mux = count_snapshot[15:8];
        		2'd2: spi_tx_mux = count_snapshot[23:16];
        		2'd3: spi_tx_mux = count_snapshot[31:24];
        		default: spi_tx_mux = 8'h00;
   	 	endcase
	end

	assign spi_tx_data = spi_tx_mux;

	always @(posedge clk) begin
    		if (!por_resetn) begin
        		spi_rx_valid_d <= 1'b0;
        		read_sel <= 2'd0;
        		count_snapshot <= 32'd0;
    		end else begin
        		spi_rx_valid_d <= spi_rx_valid;

        		if (spi_rx_valid && !spi_rx_valid_d) begin
           		 case (spi_rx_data)
               		 8'hA0: begin
                   		 count_snapshot <= pulses_sent;
                    		read_sel <= 2'd0;
                end

                8'hA1: read_sel <= 2'd1;
                8'hA2: read_sel <= 2'd2;
                8'hA3: read_sel <= 2'd3;

                default: begin
                end
            endcase
        end
    end
end
    
    reg event_status = 0;
    reg event_active = 0;

    localparam [31:0] HALF_PERIOD_LIMIT = 314;;
    localparam [31:0] BURST_PULSES = 10000;

    assign clk_en = 1'b1;
    assign LED_en = 1'b1;
    assign EVENT_OE = 1'b1;
    
    assign LED       = led_status;
	assign EVENT_OUT = event_status;
	
    always @(posedge clk) begin

        // Onboard FPGA LED
        if (led_counter >= 49_999_999) begin
            led_counter <= 0;
            led_status <= ~led_status;
        end else begin
            led_counter <= led_counter + 1'b1;
        end
		
        start_meta <= START;
        start_sync <= start_meta;
        start_prev <= start_sync;

        if (start_sync && !start_prev) begin
            event_counter <= 0;
            pulses_sent <= 0;
            event_status <= 0;
            event_active <= 1;
        end else if (event_active) begin

            if (event_counter >= HALF_PERIOD_LIMIT) begin
                event_counter <= 0;

                if (!event_status) begin
                    event_status <= 1;
                    pulses_sent <= pulses_sent + 1'b1;
                end else begin
                    event_status <= 0;

                    if (pulses_sent >= BURST_PULSES) begin
                        event_active <= 0;
                    end
                end

            end else begin
                event_counter <= event_counter + 1'b1;
            end

        end else begin
            event_status <= 0;
            event_counter <= 0;
        end

    end

endmodule
