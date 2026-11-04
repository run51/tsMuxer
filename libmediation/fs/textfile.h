#ifndef LIBMEDIATION_TEXTFILE_H_
#define LIBMEDIATION_TEXTFILE_H_

#include "file.h"

class TextFile final : public File
{
   public:
    TextFile() = default;
    TextFile(const char* fName, const unsigned int oflag, const unsigned int systemDependentFlags = 0)
        : File(fName, oflag, systemDependentFlags)
    {
    }

    bool readLine(std::string& line) const
    {
        line = "";
        char ch;
        int readCnt;
        do
        {
            readCnt = read(&ch, 1);
        } while (readCnt == 1 && (ch == '\n' || ch == '\r'));
        if (readCnt != 1)
            return false;
        if (ch != '\n' && ch != '\r')
            line += ch;
        while (true)
        {
            readCnt = read(&ch, 1);
            if (readCnt == 1 && ch != '\n' && ch != '\r')
                line += ch;
            else
                break;
        }
        // files saved as "UTF-8 with BOM" (Notepad, PowerShell) start with EF BB BF
        if (line.compare(0, 3, "\xEF\xBB\xBF") == 0)
            line.erase(0, 3);
        return readCnt == 1 || !line.empty();
    }
    bool writeLine(const std::string& line)
    {
        if (write(line.c_str(), static_cast<uint32_t>(line.size())) != static_cast<int32_t>(line.size()))
            return false;
        return write("\r\n", 2) == 2;
    }
};

#endif
