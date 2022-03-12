#pragma once

using namespace System;

namespace auoSetup {

	public ref class CheckDummy {
	public:
		int GetIntTimes10(int x) {
			String^ str = x.ToString();
			str += L"0";
			return Convert::ToInt32(str);
		}
	};
}