library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity tb_dma_controller is
end entity tb_dma_controller;

architecture sim of tb_dma_controller is

    constant ADDR_WIDTH : integer := 32;
    constant DATA_WIDTH : integer := 32;
    constant CLK_PERIOD : time := 10 ns;

    constant MEM_WORDS : integer := 256; -- velicina simulirane memorije (u recima)

    -- ---- signali ka DMA kontroleru ----
    signal clk   : std_logic := '0';
    signal rst_n : std_logic := '0';

    signal start       : std_logic := '0';
    signal src_addr_in : std_logic_vector(ADDR_WIDTH-1 downto 0) := (others => '0');
    signal dst_addr_in : std_logic_vector(ADDR_WIDTH-1 downto 0) := (others => '0');
    signal length_in   : std_logic_vector(31 downto 0) := (others => '0');

    signal busy, done, dma_error : std_logic;

    signal m_axi_araddr  : std_logic_vector(ADDR_WIDTH-1 downto 0);
    signal m_axi_arvalid : std_logic;
    signal m_axi_arready : std_logic := '0';

    signal m_axi_rdata  : std_logic_vector(DATA_WIDTH-1 downto 0) := (others => '0');
    signal m_axi_rvalid : std_logic := '0';
    signal m_axi_rready : std_logic;
    signal m_axi_rresp  : std_logic_vector(1 downto 0) := "00";

    signal m_axi_awaddr  : std_logic_vector(ADDR_WIDTH-1 downto 0);
    signal m_axi_awvalid : std_logic;
    signal m_axi_awready : std_logic := '0';

    signal m_axi_wdata  : std_logic_vector(DATA_WIDTH-1 downto 0);
    signal m_axi_wvalid : std_logic;
    signal m_axi_wready : std_logic := '0';

    signal m_axi_bvalid : std_logic := '0';
    signal m_axi_bready : std_logic;
    signal m_axi_bresp  : std_logic_vector(1 downto 0) := "00";

    -- ---- simulirana memorija (deljena za citanje i pisanje) ----
    type mem_array_t is array (0 to MEM_WORDS-1) of std_logic_vector(DATA_WIDTH-1 downto 0);
    signal mem : mem_array_t := (others => (others => '0'));

    signal sim_finished : boolean := false;

begin

    -- =====================================================================
    -- DUT (Device Under Test) - nas DMA kontroler
    -- =====================================================================
    dut: entity work.dma_controller
        generic map (
            ADDR_WIDTH => ADDR_WIDTH,
            DATA_WIDTH => DATA_WIDTH
        )
        port map (
            clk   => clk,
            rst_n => rst_n,

            start       => start,
            src_addr_in => src_addr_in,
            dst_addr_in => dst_addr_in,
            length_in   => length_in,

            busy  => busy,
            done  => done,
            error => dma_error,

            m_axi_araddr  => m_axi_araddr,
            m_axi_arvalid => m_axi_arvalid,
            m_axi_arready => m_axi_arready,

            m_axi_rdata  => m_axi_rdata,
            m_axi_rvalid => m_axi_rvalid,
            m_axi_rready => m_axi_rready,
            m_axi_rresp  => m_axi_rresp,

            m_axi_awaddr  => m_axi_awaddr,
            m_axi_awvalid => m_axi_awvalid,
            m_axi_awready => m_axi_awready,

            m_axi_wdata  => m_axi_wdata,
            m_axi_wvalid => m_axi_wvalid,
            m_axi_wready => m_axi_wready,

            m_axi_bvalid => m_axi_bvalid,
            m_axi_bready => m_axi_bready,
            m_axi_bresp  => m_axi_bresp
        );

    -- =====================================================================
    -- Generator takta
    -- =====================================================================
    clk_gen: process
    begin
        while not sim_finished loop
            clk <= '0';
            wait for CLK_PERIOD / 2;
            clk <= '1';
            wait for CLK_PERIOD / 2;
        end loop;
        wait;
    end process;

    -- =====================================================================
    -- Ponasanje "slave" strane AXI4-Lite magistrale - jednostavan model
    -- memorije koji odgovara na citanje i pisanje sa po 1 taktom kasnjenja,
    -- da simulacija bude realisticna (nije sve "trenutno").
    -- =====================================================================
    axi_slave: process (clk, rst_n)
        variable addr_word : integer;
    begin
        if rst_n = '0' then
            m_axi_arready <= '0';
            m_axi_rvalid  <= '0';
            m_axi_awready <= '0';
            m_axi_wready  <= '0';
            m_axi_bvalid  <= '0';
        elsif rising_edge(clk) then

            -- ---- kanal citanja adrese: uvek spremni da prihvatimo adresu ----
            m_axi_arready <= '1';

            -- ---- kanal citanja podataka ----
            if m_axi_arvalid = '1' and m_axi_arready = '1' then
                addr_word := to_integer(unsigned(m_axi_araddr)) / (DATA_WIDTH/8);
                m_axi_rdata  <= mem(addr_word);
                m_axi_rresp  <= "00"; -- OKAY
                m_axi_rvalid <= '1';
            elsif m_axi_rvalid = '1' and m_axi_rready = '1' then
                m_axi_rvalid <= '0';
            end if;

            -- ---- kanal adrese pisanja ----
            m_axi_awready <= '1';

            -- ---- kanal podataka pisanja: kad stigne validan podatak, upisi u mem ----
            if m_axi_awvalid = '1' and m_axi_awready = '1' then
                m_axi_wready <= '1';
            end if;

            if m_axi_wvalid = '1' and m_axi_wready = '1' then
                addr_word := to_integer(unsigned(m_axi_awaddr)) / (DATA_WIDTH/8);
                mem(addr_word) <= m_axi_wdata;
                m_axi_wready <= '0';
                m_axi_bvalid <= '1';
                m_axi_bresp  <= "00"; -- OKAY
            elsif m_axi_bvalid = '1' and m_axi_bready = '1' then
                m_axi_bvalid <= '0';
            end if;

        end if;
    end process;

    -- =====================================================================
    -- Glavni test scenario
    -- =====================================================================
    stimulus: process
        constant SRC_BASE  : integer := 0;   -- rec-indeks izvora
        constant DST_BASE  : integer := 64;  -- rec-indeks odredista (razlicit deo mem)
        constant NUM_WORDS : integer := 16;
        variable ok : boolean := true;
    begin
        report "=== TB start: DMA controller test ===";

        -- Reset
        rst_n <= '0';
        wait for 5 * CLK_PERIOD;
        rst_n <= '1';
        wait for 2 * CLK_PERIOD;

        -- 1) Upisi prepoznatljiv test-pattern na izvorne adrese
        for i in 0 to NUM_WORDS - 1 loop
            mem(SRC_BASE + i) <= std_logic_vector(to_unsigned(16#1234_0000# + i, DATA_WIDTH));
        end loop;
        wait for 2 * CLK_PERIOD;

        -- 2) Zadaj komandu DMA kontroleru
        src_addr_in <= std_logic_vector(to_unsigned(SRC_BASE * (DATA_WIDTH/8), ADDR_WIDTH));
        dst_addr_in <= std_logic_vector(to_unsigned(DST_BASE * (DATA_WIDTH/8), ADDR_WIDTH));
        length_in   <= std_logic_vector(to_unsigned(NUM_WORDS, 32));

        wait until rising_edge(clk);
        start <= '1';
        wait until rising_edge(clk);
        start <= '0';

        report "Komanda poslata, DMA treba sada da radi samostalno (CPU 'radi drugi posao')...";

        -- 3) Sacekaj signal done (sa limitom, da simulacija ne visi zauvek)
        for i in 0 to 5000 loop
            wait until rising_edge(clk);
            exit when done = '1';
        end loop;

        assert done = '1'
            report "GRESKA: DMA nikad nije zavrsio (done nikad nije bio 1)!"
            severity failure;

        report "DMA je zavrsio prenos (done=1). Proveravam podatke...";

        wait for 2 * CLK_PERIOD;

        -- 4) Proveri da su podaci na odredistu identicni izvoru
        for i in 0 to NUM_WORDS - 1 loop
            if mem(DST_BASE + i) /= mem(SRC_BASE + i) then
                ok := false;
                report "MISMATCH na reci " & integer'image(i) &
                       " : ocekivano " & integer'image(to_integer(unsigned(mem(SRC_BASE + i)))) &
                       " dobijeno "  & integer'image(to_integer(unsigned(mem(DST_BASE + i))))
                       severity error;
            end if;
        end loop;

        if ok then
            report "=== USPEH: svih " & integer'image(NUM_WORDS) & " reci ispravno preneto preko DMA! ===";
        else
            report "=== NEUSPEH: podaci se ne poklapaju! ===" severity failure;
        end if;

        sim_finished <= true;
        wait;
    end process;

end architecture sim;
