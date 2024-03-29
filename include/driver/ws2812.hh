#ifndef STM32G4_LIBS_WS2812_HH_
#define STM32G4_LIBS_WS2812_HH_

#include <utl/utl.hh>
#include <utl/color.hh>
#include <utl/interface/hal/driver.hh>

#include "hal.hh"
#include "stm32g4xx_hal_dma.h"
#include "stm32g4xx_hal_spi.h"

namespace stm32g4::driver {

namespace detail::ws2812 {

template <typename T>
struct decorate_dma_channel : T {
    decorate_dma_channel(DMA_Channel_TypeDef* instance, dma::request req) :
        T{instance, req, dma::direction::memory_to_periph, 
        dma::p_increment::disable, dma::m_increment::enable,
        dma::p_align::hword, dma::m_align::hword, 
        dma::mode::normal, dma::priority::very_high} 
    {}
};

} //namespace detail::ws2812


//FIXME: might want to factor out some of the drawing responsibilities; this
//class has too many things going on.
template <uint32_t N_LEDS, typename Pwm, typename DmaChannel, typename Color_t = utl::color::rgb>
class ws2812 : public utl::interface::hal::driver {
public:
    using dma_channel_t = detail::ws2812::decorate_dma_channel<DmaChannel>;
    using pwm_channel_t = typename Pwm::dma_channel_t;
    using time_t = typename pwm_channel_t::time_t;

    static_assert(std::is_same_v<time_t,utl::unit::duration::ns>, "ws2812 requires a pwm source with a precision of nanoseconds!");
private:
    Pwm&                            m_pwm_source;
    pwm_channel_t&                  m_pwm_channel;
    dma_channel_t&                  m_dma_channel;
    Color_t                         m_color_data[N_LEDS];
    //FIXME: express the reset time at the beginning more clearly.
    mutable uint16_t                m_write_buffer[N_LEDS*3*8 + 10];
protected:

    ws2812(Pwm& pwm_source, pwm_channel_t& pwm_channel, dma_channel_t& dma_channel)
        : m_pwm_source{pwm_source}, m_pwm_channel{pwm_channel}, m_dma_channel{dma_channel},
        m_color_data{}, m_write_buffer{}
    {
        m_pwm_channel.set_polarity(Pwm::polarity_t::ACTIVE_LOW);
        m_pwm_source.set_period(1250_ns);
        
        m_pwm_channel.link_dma(m_dma_channel);
    }

    utl::result<void> validate() {
        return m_pwm_source.start();
    }

    uint16_t get_width(bool bit) const {
        if(bit) {
            return m_pwm_channel.width_to_dma_value(600_ns);
        } else {
            return m_pwm_channel.width_to_dma_value(300_ns);
        }
    }
public:
    Color_t& operator[](size_t idx) { return m_color_data[idx]; }

    constexpr size_t count() const { return N_LEDS; }

    void service_dma() {
        m_dma_channel.service();
    }

    utl::result<void> write() const {
        uint16_t active_light = N_LEDS;
        uint8_t bit = 0;
        const uint8_t skip_first = 9;

        while(active_light) {
            while(bit < 24) {
                uint32_t bit_shift = 23 - bit;
                uint32_t read_index = active_light - 1;
                uint32_t write_index = (N_LEDS - read_index - 1)*24 + bit + skip_first;
                m_write_buffer[write_index] 
                    = get_width((static_cast<utl::color::rgb>(m_color_data[read_index]).data >> bit_shift) & 0b1);
                bit++;                

                
            }
            active_light--;
            bit = 0;
        }

        m_write_buffer[sizeof(m_write_buffer)/sizeof(m_write_buffer[0]) - 1] = 0;        
        return m_pwm_channel.start(reinterpret_cast<uint32_t*>(m_write_buffer), sizeof(m_write_buffer)/sizeof(m_write_buffer[0]));
    }
};

} //namespace stm32g4::driver

#endif //STM32G4_LIBS_WS2812_HH_
