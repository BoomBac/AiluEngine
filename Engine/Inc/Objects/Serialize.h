#ifndef __SERIALIZE_H__
#define __SERIALIZE_H__
#include "GlobalMarco.h"

#include <iostream>
#include <sstream>
#include <string>
#include <type_traits>

namespace Ailu
{
    class Archive;
    class AILU_API IPersistentable
    {
    public:
        virtual ~IPersistentable() = default;
        virtual void Serialize(Archive &arch) = 0;
        virtual void Deserialize(Archive &arch) = 0;
    };
    class Archive
    {
    public:
        Archive(std::ostream *os) : _os(os) {};
        Archive(std::istream *is) : _is(is) {};

        std::ostream &GetOStream() { return *_os; }
        std::istream &GetIStream() { return *_is; }

        virtual void InsertIndent(){};
        virtual void NewLine(){};
        virtual const String &GetIndent() const
        { 
            static String ret = "";
            return ret;
        };
        virtual void IncreaseIndent() {};
        virtual void DecreaseIndent() {};
        template<typename T>
        std::ostream &operator<<(const T &obj)
        {
            (*_os) << obj;
            return (*_os);
        }
        template<typename T>
        std::ostream &operator<<(T *obj)
        {
            (*_os) << (*obj);
            return (*_os);
        }
        template<>
        std::ostream &operator<<(const char *obj)
        {
            (*_os) << obj;
            return (*_os);
        }
        template<typename T>
        std::istream &operator>>(T &obj)
        {
            (*_is) >> obj;
            return (*_is);
        }
        // std::ostream &operator<<(IPersistentable &obj)
        // {
        //     obj.Serialize(*this);
        //     return GetOStream();
        // }
        // std::istream &operator>>(IPersistentable &obj)
        // {
        //     obj.Deserialize(*this);
        //     return GetIStream();
        // }

    protected:
        std::ostream *_os;
        std::istream *_is;
        u32 _version = 0;
    };
    struct IndentBlock
    {
        IndentBlock(Archive &arch) : _arch(arch) 
        { 
            arch.IncreaseIndent();
        }
        ~IndentBlock() { _arch.DecreaseIndent(); }
        Archive& _arch;
    };
    class TextOArchive : public Archive
    {
    public:
        TextOArchive(std::ostream *os) : Archive(os), _indentLevel(0) {}
        void InsertIndent() final
        {
            GetOStream() << indents[_indentLevel];
        }

        void NewLine() final
        {
            GetOStream() << "\n";
        }
        const String &GetIndent() const final { return indents[_indentLevel]; }
        void IncreaseIndent() final { ++_indentLevel; }
        void DecreaseIndent() final
        {
            if (_indentLevel > 0) --_indentLevel;
        }

    private:
        int _indentLevel;
        inline static String indents[] = {"", "    ", "        ", "            ", "                ", "                    "};
    };

    class TextIArchive : public Archive
    {
    public:
        TextIArchive(std::istream *is) : Archive(is) {}
        void InsertIndent() override {}
        void NewLine() override {}
        void IncreaseIndent() override {}
        void DecreaseIndent() override {}

    private:
    };

    //--------------------------------------------------------------basic save/load------------------------------------------------------------
}// namespace Ailu
#endif//