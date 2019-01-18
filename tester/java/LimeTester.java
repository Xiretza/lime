import org.linphone.lime.*;

import javax.net.ssl.*;
import java.util.UUID;
import java.security.cert.X509Certificate;

import java.io.File;

/**
 * @brief For testing purpose, we just count the success and fail received
 */
class LimeStatusCallbackImpl implements LimeStatusCallback {
	public int success;
	public int fail;

	/**
	 * @brief Function called by native code when asynchronous processing is completed
	 *
	 * @param[in]	status	an integer mapped lime:CallbackReturn, use LimeCallbackReturn.fromNative to turn it into a java enumeration
	 * @param[in]	message	a string message giving some details in case of failure
	 */
	public void callback(int cstatus, String message) {
		LimeCallbackReturn status = LimeCallbackReturn.fromNative(cstatus);
		if (status == LimeCallbackReturn.SUCCESS) {
			success++;
		} else { // status is LimeCallbackReturn.FAIL
			fail++;
		}
	}

	public void resetStatus() {
		success = 0;
		fail = 0;
	}

	public LimeStatusCallbackImpl() {
		resetStatus();
	}
}

public class LimeTester {
	public static void main(String[] args) {
		// Load lime library
		try {
			System.loadLibrary("lime");
		} catch (Exception e) {
			System.out.println("Exception while loading peer library" + e.getMessage());
			System.exit(-1);
		}

		// delete testing db files
		File file = new File("testdb_Alice.sqlite3");
		file.delete();
		file = new File("testdb_Bob.sqlite3");
		file.delete();

		// INSECURE CODE - USED FOR TESTING ONLY - DO NOT USE THIS IN REAL SITUATION
		// Disable SSL certificate verification
		TrustManager[] trustAllCerts = new TrustManager[] {
			new X509TrustManager() {
				public java.security.cert.X509Certificate[] getAcceptedIssuers() {
					return new X509Certificate[0];
				}
				public void checkClientTrusted(java.security.cert.X509Certificate[] certs, String authType) { }
				public void checkServerTrusted(java.security.cert.X509Certificate[] certs, String authType) { }
			}
		};

		try {
			SSLContext sc = SSLContext.getInstance("SSL");
			sc.init(null, trustAllCerts, new java.security.SecureRandom());
			HttpsURLConnection.setDefaultSSLSocketFactory(sc.getSocketFactory());
		} catch (Exception e) {
			System.out.println("Exception while initializing the trustAll certificates" + e.getMessage());
			System.exit(-1);
		}
		// End of INSECURE CODE - USED FOR TESTING ONLY - DO NOT USE THIS IN REAL SITUATION
		int expected_success = 0;
		int expected_fail = 0;

		String AliceDeviceId = "alice"+UUID.randomUUID().toString();
		String BobDeviceId = "bob"+UUID.randomUUID().toString();
		LimeManager aliceManager = new LimeManager("testdb_Alice.sqlite3");
		LimeManager bobManager = new LimeManager("testdb_Bob.sqlite3");

		expected_success+= 2;
		LimeStatusCallbackImpl statusCallback = new LimeStatusCallbackImpl();
		aliceManager.create_user(AliceDeviceId, "https://localhost:25519", LimeCurveId.C25519, statusCallback);
		bobManager.create_user(BobDeviceId, "https://localhost:25519", LimeCurveId.C25519, 10, statusCallback);
		assert (statusCallback.success == expected_success);
		assert (statusCallback.fail == expected_fail);

		// prepare for encrypt
		RecipientData[] recipients = new RecipientData[1]; // 1 target for the encrypt
		String plainMessage = "Moi je connais un ami il s'appelle Alceste";
		recipients[0] = new RecipientData(BobDeviceId);
		LimeOutputBuffer cipherMessage = new LimeOutputBuffer();

		expected_success+= 1;
		aliceManager.encrypt(AliceDeviceId, "bob", recipients, plainMessage.getBytes(), cipherMessage, statusCallback);
		assert (recipients[0].getPeerStatus() == LimePeerDeviceStatus.UNKNOWN):"Encryption status for Bob is expected to be UNKNOWN"; // Bob is unknown in Alice's base
		assert (cipherMessage.buffer.length==0):"Default encryption policy implies no cipherMessage for one recipient only";
		assert (statusCallback.success == expected_success);
		assert (statusCallback.fail == expected_fail);

		LimeOutputBuffer decodedMessage = new LimeOutputBuffer();
		// decrypt and check we get back to the original
		LimePeerDeviceStatus status = bobManager.decrypt(BobDeviceId, "bob", AliceDeviceId, recipients[0].DRmessage, decodedMessage);
		String s = new String(decodedMessage.buffer);
		assert (s.equals(plainMessage)):"Decoded message is not the encoded one";
		assert (status == LimePeerDeviceStatus.UNKNOWN):"decrypt status was expected to be unknown but is not";

		// encrypt another message
		recipients = new RecipientData[1]; // 1 target for the encrypt
		plainMessage = "Nous, on l'appelle Zantrop c'est note ami Zantrop";
		recipients[0] = new RecipientData(AliceDeviceId);
		cipherMessage = new LimeOutputBuffer();

		expected_success+= 1;
		bobManager.encrypt(BobDeviceId, "alice", recipients, plainMessage.getBytes(), cipherMessage, statusCallback, LimeEncryptionPolicy.CIPHERMESSAGE);
		assert (recipients[0].getPeerStatus() == LimePeerDeviceStatus.UNTRUSTED):"Encryption status for Bob is expected to be UNTRUSTED"; // Alice status is untrusted in Bob's base
		assert (cipherMessage.buffer.length>0):"Forced cipherMessage encryption policy must produce a cipherMessage even for one recipient only";
		assert (statusCallback.success == expected_success);
		assert (statusCallback.fail == expected_fail);


		decodedMessage = new LimeOutputBuffer();
		// decrypt and check we get back to the original
		status = aliceManager.decrypt(AliceDeviceId, "alice", BobDeviceId, recipients[0].DRmessage, cipherMessage.buffer, decodedMessage);
		s = new String(decodedMessage.buffer);
		assert (s.equals(plainMessage)):"Decoded message is not the encoded one";
		assert (status == LimePeerDeviceStatus.UNTRUSTED):"decrypt status was expected to be untrusted but is not";

		expected_success+= 2;
		aliceManager.update(statusCallback);
		bobManager.update(statusCallback);
		assert (statusCallback.success == expected_success);
		assert (statusCallback.fail == expected_fail);

		// clean db
		expected_success+= 2;
		aliceManager.delete_user(AliceDeviceId, statusCallback);
		bobManager.delete_user(BobDeviceId, statusCallback);
		assert (statusCallback.success == expected_success);
		assert (statusCallback.fail == expected_fail);

		// Force garbage collection to be sure the native object destructor is called
		aliceManager = null;
		bobManager = null;
		System.gc();

		System.exit(0);
	}
}
