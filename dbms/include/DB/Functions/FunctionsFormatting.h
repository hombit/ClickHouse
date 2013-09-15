#include <DB/Functions/IFunction.h>
#include "NumberTraits.h"
#include <DB/Columns/ColumnString.h>
#include <DB/DataTypes/DataTypesNumberFixed.h>
#include <DB/IO/WriteBufferFromVector.h>
#include <DB/IO/WriteBufferFromString.h>


namespace DB
{

/** Функция для необычного преобразования в строку:
	* 
	* bitmaskToList - принимает целое число - битовую маску, возвращает строку из степеней двойки через запятую.
	* 					например, bitmaskToList(50) = '2,16,32'
	*/

class FunctionBitmaskToList : public IFunction
{
public:
	/// Получить основное имя функции.
	virtual String getName() const
	{
		return "bitmaskToList";
	}
	
	/// Получить тип результата по типам аргументов. Если функция неприменима для данных аргументов - кинуть исключение.
	virtual DataTypePtr getReturnType(const DataTypes & arguments) const
	{
		if (arguments.size() != 1)
			throw Exception("Number of arguments for function " + getName() + " doesn't match: passed "
			+ toString(arguments.size()) + ", should be 1.",
							ErrorCodes::NUMBER_OF_ARGUMENTS_DOESNT_MATCH);
			
		const IDataType * type = &*arguments[0];
		
		if (!dynamic_cast<const DataTypeUInt8 *>(type) &&
			!dynamic_cast<const DataTypeUInt16 *>(type) &&
			!dynamic_cast<const DataTypeUInt32 *>(type) &&
			!dynamic_cast<const DataTypeUInt64 *>(type) &&
			!dynamic_cast<const DataTypeInt8 *>(type) &&
			!dynamic_cast<const DataTypeInt16 *>(type) &&
			!dynamic_cast<const DataTypeInt32 *>(type) &&
			!dynamic_cast<const DataTypeInt64 *>(type))
			throw Exception("Cannot format " + type->getName() + " as bitmask string", ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT);
		
		return new DataTypeString;
	}
	
	template<typename T>
	inline static void writeBitmask(T x, WriteBuffer & out)
	{
		bool first = true;
		while (x)
		{
			T y = (x & (x - 1));
			T bit = x ^ y;
			x = y;
			if (!first)
				out.write(",", 1);
			first = false;
			writeIntText(bit, out);
		}
	}
	
	template<typename T>
	bool executeType(Block & block, const ColumnNumbers & arguments, size_t result)
	{
		if (const ColumnVector<T> * col_from = dynamic_cast<const ColumnVector<T> *>(&*block.getByPosition(arguments[0]).column))
		{
			ColumnString * col_to = new ColumnString;
			block.getByPosition(result).column = col_to;
			
			const typename ColumnVector<T>::Container_t & vec_from = col_from->getData();
			ColumnString::Chars_t & data_to = col_to->getChars();
			ColumnString::Offsets_t & offsets_to = col_to->getOffsets();
			size_t size = vec_from.size();
			data_to.resize(size * 2);
			offsets_to.resize(size);

			WriteBufferFromVector<ColumnString::Chars_t> buf_to(data_to);

			for (size_t i = 0; i < size; ++i)
			{
				writeBitmask<T>(vec_from[i], buf_to);
				writeChar(0, buf_to);
				offsets_to[i] = buf_to.count();
			}
			data_to.resize(buf_to.count());
		}
		else if (const ColumnConst<T> * col_from = dynamic_cast<const ColumnConst<T> *>(&*block.getByPosition(arguments[0]).column))
		{
			std::string res;
			{
				WriteBufferFromString buf(res);
				writeBitmask<T>(col_from->getData(), buf);
			}
			
			block.getByPosition(result).column = new ColumnConstString(col_from->size(), res);
		}
		else
		{
			return false;
		}
		
		return true;
	}
	
	/// Выполнить функцию над блоком.
	void execute(Block & block, const ColumnNumbers & arguments, size_t result)
	{
		if (!(	executeType<UInt8>(block, arguments, result)
			||	executeType<UInt16>(block, arguments, result)
			||	executeType<UInt32>(block, arguments, result)
			||	executeType<UInt64>(block, arguments, result)
			||	executeType<Int8>(block, arguments, result)
			||	executeType<Int16>(block, arguments, result)
			||	executeType<Int32>(block, arguments, result)
			||	executeType<Int64>(block, arguments, result)))
			throw Exception("Illegal column " + block.getByPosition(arguments[0]).column->getName()
			+ " of argument of function " + getName(),
							ErrorCodes::ILLEGAL_COLUMN);
	}
private:
	
};

}
