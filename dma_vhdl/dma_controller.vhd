--------------------------------------------------------------------------------
-- dma_controller.vhd
--------------------------------------------------------------------------------
-- Jednostavan DMA kontroler koji autonomno prenosi niz reci podataka sa
-- izvorne (src) na odredisnu (dst) adresu, preko AXI4-Lite Master interfejsa,
-- bez ucesca CPU-a tokom samog prenosa.
--
-- NAPOMENA O POJEDNOSTAVLJENJU: koristi se AXI4-LITE stil magistrale (jedna
-- rec podataka po "rundi" citanje/pisanje), a ne puni AXI4 burst protokol.
-- Princip razgovora (adresa -> handshake -> podatak -> potvrda) je identican
-- punom AXI4, samo bez burst duzine (AWLEN/ARLEN) i ID signala. Ovo je
-- namerno pojednostavljenje radi jasnoce i lakse verifikacije; dogradnja na
-- puni burst AXI4 bi znacila dodavanje brojaca duzine burst-a po transakciji
-- umesto po pojedinacnoj reci.
--
-- FSM prolazi kroz stanja:
--   IDLE   -> ceka komandu (start=1)
--   SETUP  -> ucitava src/dst adrese i duzinu iz ulaznih registara
--   RADDR  -> salje adresu citanja, ceka da slave kaze "spreman sam" (ARREADY)
--   RDATA  -> ceka da slave vrati podatak (RVALID), cuva ga
--   WADDR  -> salje adresu pisanja, ceka WAWREADY... (AWREADY)
--   WDATA  -> salje podatak za pisanje, ceka WREADY
--   WRESP  -> ceka potvrdu pisanja (BVALID)
--   NEXTW  -> proverava da li ima jos reci; ako da, nazad na RADDR
--   DONE_S -> javlja CPU-u da je gotovo (done=1 na jedan takt), vraca se IDLE
--------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity dma_controller is
    generic (
        ADDR_WIDTH : integer := 32;
        DATA_WIDTH : integer := 32
    );
    port (
        clk   : in std_logic;
        rst_n : in std_logic; -- aktivan na 0 (nula = reset)

        -- ==================== Kontrolni interfejs (od CPU-a) ====================
        start        : in  std_logic;
        src_addr_in  : in  std_logic_vector(ADDR_WIDTH-1 downto 0);
        dst_addr_in  : in  std_logic_vector(ADDR_WIDTH-1 downto 0);
        length_in    : in  std_logic_vector(31 downto 0); -- broj reci za prenos

        -- ==================== Statusni interfejs (ka CPU-u) ====================
        busy  : out std_logic;
        done  : out std_logic; -- puls, jedan takt, kad se prenos zavrsi
        error : out std_logic; -- AXI je vratio gresku (RRESP/BRESP != OKAY)

        -- ==================== AXI4-Lite Master: kanal za citanje adrese ====================
        m_axi_araddr  : out std_logic_vector(ADDR_WIDTH-1 downto 0);
        m_axi_arvalid : out std_logic;
        m_axi_arready : in  std_logic;

        -- ==================== AXI4-Lite Master: kanal za citanje podataka ====================
        m_axi_rdata  : in  std_logic_vector(DATA_WIDTH-1 downto 0);
        m_axi_rvalid : in  std_logic;
        m_axi_rready : out std_logic;
        m_axi_rresp  : in  std_logic_vector(1 downto 0);

        -- ==================== AXI4-Lite Master: kanal za adresu pisanja ====================
        m_axi_awaddr  : out std_logic_vector(ADDR_WIDTH-1 downto 0);
        m_axi_awvalid : out std_logic;
        m_axi_awready : in  std_logic;

        -- ==================== AXI4-Lite Master: kanal za podatke pisanja ====================
        m_axi_wdata  : out std_logic_vector(DATA_WIDTH-1 downto 0);
        m_axi_wvalid : out std_logic;
        m_axi_wready : in  std_logic;

        -- ==================== AXI4-Lite Master: kanal odgovora na pisanje ====================
        m_axi_bvalid : in  std_logic;
        m_axi_bready : out std_logic;
        m_axi_bresp  : in  std_logic_vector(1 downto 0)
    );
end entity dma_controller;

architecture rtl of dma_controller is

    -- -------------------------------------------------------------------
    -- Stanja FSM-a - ovo su tacno "faze" koje smo opisali principijelno:
    -- IDLE / SETUP / citaj / pisi (u petlji) / DONE
    -- -------------------------------------------------------------------
    type state_t is (IDLE, SETUP, RADDR, RDATA, WADDR, WDATA, WRESP, NEXTW, DONE_S, ERROR_S);
    signal state, state_next : state_t;

    constant AXI_OKAY : std_logic_vector(1 downto 0) := "00";

    -- Interni registri - ovo je hardverski ekvivalent onoga sto je u nasem
    -- C++ alokatoru bio "offset_": mala memorija koja pamti napredak posla.
    signal src_reg   : unsigned(ADDR_WIDTH-1 downto 0);
    signal dst_reg   : unsigned(ADDR_WIDTH-1 downto 0);
    signal words_left: unsigned(31 downto 0);
    signal data_hold : std_logic_vector(DATA_WIDTH-1 downto 0);

begin

    -- =====================================================================
    -- Proces 1: registar stanja (samo pamti trenutno stanje, sinhrono)
    -- =====================================================================
    process (clk, rst_n)
    begin
        if rst_n = '0' then
            state <= IDLE;
        elsif rising_edge(clk) then
            state <= state_next;
        end if;
    end process;

    -- =====================================================================
    -- Proces 2: kombinaciona logika sledeceg stanja
    -- Ovo je "mozak" - odlucuje kada se prelazi iz jedne faze u drugu,
    -- tacno prateci dijagram stanja koji smo prethodno definisali.
    -- =====================================================================
    process (state, start, m_axi_arready, m_axi_rvalid, m_axi_rresp,
             m_axi_awready, m_axi_wready, m_axi_bvalid, m_axi_bresp, words_left, length_in)
    begin
        state_next <= state; -- podrazumevano: ostani gde jesi

        case state is
            when IDLE =>
                if start = '1' then
                    state_next <= SETUP;
                end if;

            when SETUP =>
                -- VAZNO: ovde proveravamo length_in (svezu ulaznu vrednost),
                -- NE words_left - jer se words_left tek u OVOM taktu ucitava
                -- iz length_in (proces 3), pa bi provera "words_left = 0"
                -- ovde procitala staru (jos neucitanu) vrednost iz reseta.
                if unsigned(length_in) = 0 then
                    state_next <= DONE_S;
                else
                    state_next <= RADDR;
                end if;

            when RADDR =>
                if m_axi_arready = '1' then
                    state_next <= RDATA;
                end if;

            when RDATA =>
                if m_axi_rvalid = '1' then
                    if m_axi_rresp /= AXI_OKAY then
                        state_next <= ERROR_S;
                    else
                        state_next <= WADDR;
                    end if;
                end if;

            when WADDR =>
                if m_axi_awready = '1' then
                    state_next <= WDATA;
                end if;

            when WDATA =>
                if m_axi_wready = '1' then
                    state_next <= WRESP;
                end if;

            when WRESP =>
                if m_axi_bvalid = '1' then
                    if m_axi_bresp /= AXI_OKAY then
                        state_next <= ERROR_S;
                    else
                        state_next <= NEXTW;
                    end if;
                end if;

            when NEXTW =>
                if words_left = 0 then
                    state_next <= DONE_S;
                else
                    state_next <= RADDR; -- ima jos reci, vrati se na citanje
                end if;

            when DONE_S =>
                state_next <= IDLE;

            when ERROR_S =>
                state_next <= IDLE;

        end case;
    end process;

    -- =====================================================================
    -- Proces 3: registri sa podacima (src/dst adrese, brojac, podrzani bajt)
    -- Ovo je hardverski "generator adresa" iz nase seme.
    -- =====================================================================
    process (clk, rst_n)
    begin
        if rst_n = '0' then
            src_reg    <= (others => '0');
            dst_reg    <= (others => '0');
            words_left <= (others => '0');
            data_hold  <= (others => '0');
        elsif rising_edge(clk) then
            case state is
                when SETUP =>
                    -- Ucitaj komandu iz ulaznih registara (jednom, na pocetku posla)
                    src_reg    <= unsigned(src_addr_in);
                    dst_reg    <= unsigned(dst_addr_in);
                    words_left <= unsigned(length_in);

                when RDATA =>
                    if m_axi_rvalid = '1' and m_axi_rresp = AXI_OKAY then
                        data_hold <= m_axi_rdata; -- "drzimo" procitanu vrednost
                    end if;

                when WRESP =>
                    if m_axi_bvalid = '1' and m_axi_bresp = AXI_OKAY then
                        -- Uspesno upisano jedna rec - pomeri sve adrese i brojac,
                        -- tacno kao offset_ u ArenaAllocator-u.
                        src_reg    <= src_reg + (DATA_WIDTH/8);
                        dst_reg    <= dst_reg + (DATA_WIDTH/8);
                        words_left <= words_left - 1;
                    end if;

                when others =>
                    null; -- ostali registri se ne menjaju u ovim stanjima
            end case;
        end if;
    end process;

    -- =====================================================================
    -- Proces 4: izlazni AXI4-Lite signali i statusni signali
    -- Kombinaciona logika - izlazi zavise samo od trenutnog stanja.
    -- =====================================================================
    process (state, src_reg, dst_reg, data_hold, m_axi_rresp, m_axi_bresp)
    begin
        -- Podrazumevane ("neaktivne") vrednosti - sprecava "zakljucane" signale
        m_axi_araddr  <= std_logic_vector(src_reg);
        m_axi_arvalid <= '0';
        m_axi_rready  <= '0';

        m_axi_awaddr  <= std_logic_vector(dst_reg);
        m_axi_awvalid <= '0';

        m_axi_wdata   <= data_hold;
        m_axi_wvalid  <= '0';

        m_axi_bready  <= '0';

        busy  <= '1';
        done  <= '0';
        error <= '0';

        case state is
            when IDLE =>
                busy <= '0';

            when RADDR =>
                m_axi_arvalid <= '1'; -- "trazim da citam sa ove adrese"

            when RDATA =>
                m_axi_rready <= '1'; -- "spreman sam da primim podatak"

            when WADDR =>
                m_axi_awvalid <= '1'; -- "trazim da pisem na ovu adresu"

            when WDATA =>
                m_axi_wvalid <= '1'; -- "evo podatka za upis"

            when WRESP =>
                m_axi_bready <= '1'; -- "spreman sam da primim potvrdu"

            when DONE_S =>
                busy <= '0';
                done <= '1';

            when ERROR_S =>
                busy  <= '0';
                error <= '1';

            when others =>
                null;
        end case;
    end process;

end architecture rtl;
