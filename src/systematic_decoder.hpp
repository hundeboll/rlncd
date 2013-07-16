/* Copyright 2013 Martin Hundebøll <martin@hundeboll.net> */

#ifndef FOX_SYSTEMATIC_DECODER_HPP_
#define FOX_SYSTEMATIC_DECODER_HPP_

namespace kodo
{
    template<class SuperCoder>
    class systematic_decoder_info : public SuperCoder
    {
      public:
        void decode(uint8_t *symbol_data, uint8_t *symbol_id)
        {
            SuperCoder::decode(symbol_data, symbol_id);

            m_last_symbol_systematic = false;
        }

        void decode_symbol(uint8_t *symbol_data, uint32_t symbol_index)
        {
            SuperCoder::decode_symbol(symbol_data, symbol_index);

            m_last_symbol_systematic = true;
            m_last_symbol_index = symbol_index;
        }

        bool last_symbol_is_systematic() const
        {
            return m_last_symbol_systematic;
        }

        uint32_t last_symbol_index() const
        {
            return m_last_symbol_index;
        }

      protected:
        bool m_last_symbol_systematic;
        uint32_t m_last_symbol_index;
    };
};  // namespace kodo

#endif
