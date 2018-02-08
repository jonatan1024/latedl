/**
 * Enqueues multiple files to be sent to all connected clients.
 *
 * @param filenames		Array of file names.
 * @param numFiles		Number of files in array.
 * @return				Number of successfully added files.
 */
native int AddLateDownloads(char[][] filenames, int numFiles);

/**
 * Enqueues single file to be sent to all connected clients.
 *
 * @param filename		Array of file names.
 * @return				Whether the file was successfully added.
 */
native bool AddLateDownload(char[] filename);

/**
 * Called when client successfully downloaded a file. This forward might be called before AddLateDownload(s) returns!
 *
 * @param iClient		Client index. Index 0 signals removal of the file from the download queue - all clients downloaded or failed to download the file.  
 * @param filename		Name of the successfully downloaded file. 
 */
forward void OnDownloadSuccess(int iClient, char[] filename);

/**
 * Called when client failed to download a file. This forward might be called before AddLateDownload(s) returns! Check the server console for detailed failure reason!
 *
 * @param iClient		Client index. Index 0 signals that an error occured when adding the file to the download queue.
 * @param filename		Name of the successfully downloaded file. 
 */
forward void OnDownloadFailure(int iClient, char[] filename);

/**
 * Do not edit below this line!
 */
public Extension:__ext_latedl = 
{
	name = "Late Downloads",
	file = "latedl.ext",
#if defined AUTOLOAD_EXTENSIONS
	autoload = 1,
#else
	autoload = 0,
#endif
#if defined REQUIRE_EXTENSIONS
	required = 1,
#else
	required = 0,
#endif
};