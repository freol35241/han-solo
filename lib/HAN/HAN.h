#include <Arduino.h>
#include <map>
#include <CRC16.h>

namespace HAN
{
    static std::map<String, String> OBIS_CODES{
        //{"0-0.1.0.0", "Datum och tid"},
        {"1-0:1.8.0", "Mätarställning Aktiv Energi Uttag"},
        {"1-0:2.8.0", "Mätarställning Aktiv Energi Inmatning"},
        {"1-0:3.8.0", "Mätarställning Reaktiv Energi Uttag"},
        {"1-0:4.8.0", "Mätarställning Reaktiv Energi Inmatning"},
        {"1-0:1.7.0", "Aktiv Effekt Uttag"},
        {"1-0:2.7.0", "Aktiv Effekt Inmatning"},
        {"1-0:3.7.0", "Reaktiv Effekt Uttag"},
        {"1-0:4.7.0", "Reaktiv Effekt Inmatning"},
        {"1-0:21.7.0", "L1 Aktiv Effekt Uttag"},
        {"1-0:22.7.0", "L1 Aktiv Effekt Inmatning"},
        {"1-0:41.7.0", "L2 Aktiv Effekt Uttag"},
        {"1-0:42.7.0", "L2 Aktiv Effekt Inmatning"},
        {"1-0:61.7.0", "L3 Aktiv Effekt Uttag"},
        {"1-0:62.7.0", "L3 Aktiv Effekt Inmatning"},
        {"1-0:23.7.0", "L1 Reaktiv Effekt Uttag"},
        {"1-0:24.7.0", "L1 Reaktiv Effekt Inmatning"},
        {"1-0:43.7.0", "L2 Reaktiv Effekt Uttag"},
        {"1-0:44.7.0", "L2 Reaktiv Effekt Inmatning"},
        {"1-0:63.7.0", "L3 Reaktiv Effekt Uttag"},
        {"1-0:64.7.0", "L3 Reaktiv Effekt Inmatning"},
        {"1-0:32.7.0", "L1 Fasspänning"},
        {"1-0:52.7.0", "L2 Fasspänning"},
        {"1-0:72.7.0", "L3 Fasspänning"},
        {"1-0:31.7.0", "L1 Fasström"},
        {"1-0:51.7.0", "L2 Fasström"},
        {"1-0:71.7.0", "L3 Fasström"}};

    struct Payload
    {
        float value;
        String unit;
    };

    struct Telegram
    {
        String FLAG_ID;
        String id;
        String timestamp;
        std::map<String, Payload> payloads;
        bool crc = false;
    };

    int
    parse_crc(const String &tail)
    {
        return strtol(tail.substring(1).c_str(), NULL, 16);
    };

    Telegram receive_telegram(Stream &serial)
    {
        Telegram telegram;
        String part;
        part.reserve(60);

        CRC16 crc(CRC16_IBM, 0, 0, true, true);

        while (serial.available())
        {
            part = serial.readStringUntil('\n');
            part += '\n'; // Keep newline for CRC calcs

            Serial.print(part);

            if (part.startsWith("!"))
            {
                Serial.println("Found end of telegram!");

                crc.add('!');

                auto crc_parsed = parse_crc(part);
                auto crc_calculated = crc.getCRC();

                telegram.crc = crc_calculated == crc_parsed;

                if (!telegram.crc)
                {
                    Serial.println("CRC check failed:");
                    Serial.print("  Parsed: ");
                    Serial.println(crc_parsed);
                    Serial.print("  Calculated: ");
                    Serial.println(crc_calculated);
                }

                return telegram;
            }

            // Add to crc calculations
            for (auto &c : part)
            {
                crc.add(c);
            }

            // Parse
            if (part.startsWith("/"))
            {
                Serial.println("Found start of telegram!");

                telegram.FLAG_ID = part.substring(1, 4);
                telegram.id = part.substring(5);
            }
            else if (!part.isEmpty())
            {
                String obis_code = part.substring(0, part.indexOf('('));

                if (OBIS_CODES.find(obis_code) != OBIS_CODES.end())
                {
                    String name = OBIS_CODES[obis_code];

                    String payload = part.substring(part.indexOf('(') + 1, part.indexOf(')'));
                    int idx_asterisk = payload.indexOf('*');
                    telegram.payloads[name] = Payload{
                        (float)atof(payload.substring(0, idx_asterisk).c_str()),
                        payload.substring(idx_asterisk + 1)};
                }
                else if (obis_code == "0-0:1.0.0")
                {
                    String payload = part.substring(part.indexOf('(') + 1, part.indexOf(')'));
                    telegram.timestamp = payload;
                }
                else
                {
                    Serial.print("Dont know how to handle: ");
                    Serial.println(part);
                }
            }

            // Allow some slack to next if, even if its unlikely(?), we processed faster than the baudrate
            auto count = 10;
            while (count-- && !serial.available())
            {
                delay(1);
            }
        }

        return telegram;
    };
}