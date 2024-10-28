#include <stdio.h>
#include <windows.h>

int main()
{
	//����DeleteFile������
	bool ret = DeleteFile(L"C:\\1.txt");
	printf("ret = %d \n", ret);

	//��FILE_FLAG_DELETE_ON_CLOSE��־����CreateFile��
	HANDLE hFile = CreateFile(L"C:\\2.txt", DELETE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_DELETE_ON_CLOSE, nullptr);
	CloseHandle(hFile);
	printf("hFile = %d \n", hFile);

	//���Ѿ��򿪵��ļ��ϵ���SetFileInformationByHandle��
	FILE_DISPOSITION_INFO info = { 0 };
	info.DeleteFile = true;
	HANDLE hFile2 = CreateFile(L"C:\\3.txt", DELETE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
	bool ret2 = SetFileInformationByHandle(hFile2, FileDispositionInfo, &info, sizeof(info));
	CloseHandle(hFile2);
	printf("hFile2 = %d \n", hFile2);
	printf("ret2 = %d \n", ret2);

	return 0;
}