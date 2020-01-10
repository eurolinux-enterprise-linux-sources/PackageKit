// This file was generated by the Gtk# code generator.
// Any changes made will be lost if regenerated.

namespace PackageKit {

	using System;
	using System.Collections;
	using System.Runtime.InteropServices;

#region Autogenerated code
	public class Control : GLib.Object {

		[Obsolete]
		protected Control(GLib.GType gtype) : base(gtype) {}
		public Control(IntPtr raw) : base(raw) {}

		[DllImport("libpackagekit-glib.dll")]
		static extern IntPtr pk_control_new();

		public Control () : base (IntPtr.Zero)
		{
			if (GetType () != typeof (Control)) {
				CreateNativeObject (new string [0], new GLib.Value[0]);
				return;
			}
			Raw = pk_control_new();
		}

		[GLib.CDeclCallback]
		delegate void UpdatesChangedVMDelegate (IntPtr control);

		static UpdatesChangedVMDelegate UpdatesChangedVMCallback;

		static void updateschanged_cb (IntPtr control)
		{
			try {
				Control control_managed = GLib.Object.GetObject (control, false) as Control;
				control_managed.OnUpdatesChanged ();
			} catch (Exception e) {
				GLib.ExceptionManager.RaiseUnhandledException (e, false);
			}
		}

		private static void OverrideUpdatesChanged (GLib.GType gtype)
		{
			if (UpdatesChangedVMCallback == null)
				UpdatesChangedVMCallback = new UpdatesChangedVMDelegate (updateschanged_cb);
			OverrideVirtualMethod (gtype, "updates-changed", UpdatesChangedVMCallback);
		}

		[GLib.DefaultSignalHandler(Type=typeof(PackageKit.Control), ConnectionMethod="OverrideUpdatesChanged")]
		protected virtual void OnUpdatesChanged ()
		{
			GLib.Value ret = GLib.Value.Empty;
			GLib.ValueArray inst_and_params = new GLib.ValueArray (1);
			GLib.Value[] vals = new GLib.Value [1];
			vals [0] = new GLib.Value (this);
			inst_and_params.Append (vals [0]);
			g_signal_chain_from_overridden (inst_and_params.ArrayPtr, ref ret);
			foreach (GLib.Value v in vals)
				v.Dispose ();
		}

		[GLib.Signal("updates-changed")]
		public event System.EventHandler UpdatesChanged {
			add {
				GLib.Signal sig = GLib.Signal.Lookup (this, "updates-changed");
				sig.AddDelegate (value);
			}
			remove {
				GLib.Signal sig = GLib.Signal.Lookup (this, "updates-changed");
				sig.RemoveDelegate (value);
			}
		}

		[GLib.CDeclCallback]
		delegate void RestartScheduleVMDelegate (IntPtr control);

		static RestartScheduleVMDelegate RestartScheduleVMCallback;

		static void restartschedule_cb (IntPtr control)
		{
			try {
				Control control_managed = GLib.Object.GetObject (control, false) as Control;
				control_managed.OnRestartSchedule ();
			} catch (Exception e) {
				GLib.ExceptionManager.RaiseUnhandledException (e, false);
			}
		}

		private static void OverrideRestartSchedule (GLib.GType gtype)
		{
			if (RestartScheduleVMCallback == null)
				RestartScheduleVMCallback = new RestartScheduleVMDelegate (restartschedule_cb);
			OverrideVirtualMethod (gtype, "restart-schedule", RestartScheduleVMCallback);
		}

		[GLib.DefaultSignalHandler(Type=typeof(PackageKit.Control), ConnectionMethod="OverrideRestartSchedule")]
		protected virtual void OnRestartSchedule ()
		{
			GLib.Value ret = GLib.Value.Empty;
			GLib.ValueArray inst_and_params = new GLib.ValueArray (1);
			GLib.Value[] vals = new GLib.Value [1];
			vals [0] = new GLib.Value (this);
			inst_and_params.Append (vals [0]);
			g_signal_chain_from_overridden (inst_and_params.ArrayPtr, ref ret);
			foreach (GLib.Value v in vals)
				v.Dispose ();
		}

		[GLib.Signal("restart-schedule")]
		public event System.EventHandler RestartSchedule {
			add {
				GLib.Signal sig = GLib.Signal.Lookup (this, "restart-schedule");
				sig.AddDelegate (value);
			}
			remove {
				GLib.Signal sig = GLib.Signal.Lookup (this, "restart-schedule");
				sig.RemoveDelegate (value);
			}
		}

		[GLib.CDeclCallback]
		delegate void NetworkStateChangedVMDelegate (IntPtr control);

		static NetworkStateChangedVMDelegate NetworkStateChangedVMCallback;

		static void networkstatechanged_cb (IntPtr control)
		{
			try {
				Control control_managed = GLib.Object.GetObject (control, false) as Control;
				control_managed.OnNetworkStateChanged ();
			} catch (Exception e) {
				GLib.ExceptionManager.RaiseUnhandledException (e, false);
			}
		}

		private static void OverrideNetworkStateChanged (GLib.GType gtype)
		{
			if (NetworkStateChangedVMCallback == null)
				NetworkStateChangedVMCallback = new NetworkStateChangedVMDelegate (networkstatechanged_cb);
			OverrideVirtualMethod (gtype, "network-state-changed", NetworkStateChangedVMCallback);
		}

		[GLib.DefaultSignalHandler(Type=typeof(PackageKit.Control), ConnectionMethod="OverrideNetworkStateChanged")]
		protected virtual void OnNetworkStateChanged ()
		{
			GLib.Value ret = GLib.Value.Empty;
			GLib.ValueArray inst_and_params = new GLib.ValueArray (1);
			GLib.Value[] vals = new GLib.Value [1];
			vals [0] = new GLib.Value (this);
			inst_and_params.Append (vals [0]);
			g_signal_chain_from_overridden (inst_and_params.ArrayPtr, ref ret);
			foreach (GLib.Value v in vals)
				v.Dispose ();
		}

		[GLib.Signal("network-state-changed")]
		public event System.EventHandler NetworkStateChanged {
			add {
				GLib.Signal sig = GLib.Signal.Lookup (this, "network-state-changed");
				sig.AddDelegate (value);
			}
			remove {
				GLib.Signal sig = GLib.Signal.Lookup (this, "network-state-changed");
				sig.RemoveDelegate (value);
			}
		}

		[GLib.CDeclCallback]
		delegate void TransactionListChangedVMDelegate (IntPtr control);

		static TransactionListChangedVMDelegate TransactionListChangedVMCallback;

		static void transactionlistchanged_cb (IntPtr control)
		{
			try {
				Control control_managed = GLib.Object.GetObject (control, false) as Control;
				control_managed.OnTransactionListChanged ();
			} catch (Exception e) {
				GLib.ExceptionManager.RaiseUnhandledException (e, false);
			}
		}

		private static void OverrideTransactionListChanged (GLib.GType gtype)
		{
			if (TransactionListChangedVMCallback == null)
				TransactionListChangedVMCallback = new TransactionListChangedVMDelegate (transactionlistchanged_cb);
			OverrideVirtualMethod (gtype, "transaction-list-changed", TransactionListChangedVMCallback);
		}

		[GLib.DefaultSignalHandler(Type=typeof(PackageKit.Control), ConnectionMethod="OverrideTransactionListChanged")]
		protected virtual void OnTransactionListChanged ()
		{
			GLib.Value ret = GLib.Value.Empty;
			GLib.ValueArray inst_and_params = new GLib.ValueArray (1);
			GLib.Value[] vals = new GLib.Value [1];
			vals [0] = new GLib.Value (this);
			inst_and_params.Append (vals [0]);
			g_signal_chain_from_overridden (inst_and_params.ArrayPtr, ref ret);
			foreach (GLib.Value v in vals)
				v.Dispose ();
		}

		[GLib.Signal("transaction-list-changed")]
		public event System.EventHandler TransactionListChanged {
			add {
				GLib.Signal sig = GLib.Signal.Lookup (this, "transaction-list-changed");
				sig.AddDelegate (value);
			}
			remove {
				GLib.Signal sig = GLib.Signal.Lookup (this, "transaction-list-changed");
				sig.RemoveDelegate (value);
			}
		}

		[GLib.CDeclCallback]
		delegate void RepoListChangedVMDelegate (IntPtr control);

		static RepoListChangedVMDelegate RepoListChangedVMCallback;

		static void repolistchanged_cb (IntPtr control)
		{
			try {
				Control control_managed = GLib.Object.GetObject (control, false) as Control;
				control_managed.OnRepoListChanged ();
			} catch (Exception e) {
				GLib.ExceptionManager.RaiseUnhandledException (e, false);
			}
		}

		private static void OverrideRepoListChanged (GLib.GType gtype)
		{
			if (RepoListChangedVMCallback == null)
				RepoListChangedVMCallback = new RepoListChangedVMDelegate (repolistchanged_cb);
			OverrideVirtualMethod (gtype, "repo-list-changed", RepoListChangedVMCallback);
		}

		[GLib.DefaultSignalHandler(Type=typeof(PackageKit.Control), ConnectionMethod="OverrideRepoListChanged")]
		protected virtual void OnRepoListChanged ()
		{
			GLib.Value ret = GLib.Value.Empty;
			GLib.ValueArray inst_and_params = new GLib.ValueArray (1);
			GLib.Value[] vals = new GLib.Value [1];
			vals [0] = new GLib.Value (this);
			inst_and_params.Append (vals [0]);
			g_signal_chain_from_overridden (inst_and_params.ArrayPtr, ref ret);
			foreach (GLib.Value v in vals)
				v.Dispose ();
		}

		[GLib.Signal("repo-list-changed")]
		public event System.EventHandler RepoListChanged {
			add {
				GLib.Signal sig = GLib.Signal.Lookup (this, "repo-list-changed");
				sig.AddDelegate (value);
			}
			remove {
				GLib.Signal sig = GLib.Signal.Lookup (this, "repo-list-changed");
				sig.RemoveDelegate (value);
			}
		}

		[GLib.CDeclCallback]
		delegate void LockedVMDelegate (IntPtr control, bool is_locked);

		static LockedVMDelegate LockedVMCallback;

		static void locked_cb (IntPtr control, bool is_locked)
		{
			try {
				Control control_managed = GLib.Object.GetObject (control, false) as Control;
				control_managed.OnLocked (is_locked);
			} catch (Exception e) {
				GLib.ExceptionManager.RaiseUnhandledException (e, false);
			}
		}

		private static void OverrideLocked (GLib.GType gtype)
		{
			if (LockedVMCallback == null)
				LockedVMCallback = new LockedVMDelegate (locked_cb);
			OverrideVirtualMethod (gtype, "locked", LockedVMCallback);
		}

		[GLib.DefaultSignalHandler(Type=typeof(PackageKit.Control), ConnectionMethod="OverrideLocked")]
		protected virtual void OnLocked (bool is_locked)
		{
			GLib.Value ret = GLib.Value.Empty;
			GLib.ValueArray inst_and_params = new GLib.ValueArray (2);
			GLib.Value[] vals = new GLib.Value [2];
			vals [0] = new GLib.Value (this);
			inst_and_params.Append (vals [0]);
			vals [1] = new GLib.Value (is_locked);
			inst_and_params.Append (vals [1]);
			g_signal_chain_from_overridden (inst_and_params.ArrayPtr, ref ret);
			foreach (GLib.Value v in vals)
				v.Dispose ();
		}

		[GLib.Signal("locked")]
		public event PackageKit.LockedHandler Locked {
			add {
				GLib.Signal sig = GLib.Signal.Lookup (this, "locked", typeof (PackageKit.LockedArgs));
				sig.AddDelegate (value);
			}
			remove {
				GLib.Signal sig = GLib.Signal.Lookup (this, "locked", typeof (PackageKit.LockedArgs));
				sig.RemoveDelegate (value);
			}
		}

		[DllImport("libpackagekit-glib.dll")]
		static extern unsafe IntPtr pk_control_get_mime_types(IntPtr raw, out IntPtr error);

		public unsafe string GetMimeTypes() {
			IntPtr error = IntPtr.Zero;
			IntPtr raw_ret = pk_control_get_mime_types(Handle, out error);
			string ret = GLib.Marshaller.PtrToStringGFree(raw_ret);
			if (error != IntPtr.Zero) throw new GLib.GException (error);
			return ret;
		}

		[DllImport("libpackagekit-glib.dll")]
		static extern unsafe bool pk_control_get_time_since_action(IntPtr raw, int role, out uint seconds, out IntPtr error);

		public unsafe bool GetTimeSinceAction(PackageKit.RoleEnum role, out uint seconds) {
			IntPtr error = IntPtr.Zero;
			bool raw_ret = pk_control_get_time_since_action(Handle, (int) role, out seconds, out error);
			bool ret = raw_ret;
			if (error != IntPtr.Zero) throw new GLib.GException (error);
			return ret;
		}

		[DllImport("libpackagekit-glib.dll")]
		static extern unsafe ulong pk_control_get_actions(IntPtr raw, out IntPtr error);

		public unsafe ulong GetActions() {
			IntPtr error = IntPtr.Zero;
			ulong raw_ret = pk_control_get_actions(Handle, out error);
			ulong ret = raw_ret;
			if (error != IntPtr.Zero) throw new GLib.GException (error);
			return ret;
		}

		[DllImport("libpackagekit-glib.dll")]
		static extern unsafe bool pk_control_allocate_transaction_id(IntPtr raw, IntPtr tid, out IntPtr error);

		public unsafe bool AllocateTransactionId(string tid) {
			IntPtr error = IntPtr.Zero;
			bool raw_ret = pk_control_allocate_transaction_id(Handle, GLib.Marshaller.StringToPtrGStrdup(tid), out error);
			bool ret = raw_ret;
			if (error != IntPtr.Zero) throw new GLib.GException (error);
			return ret;
		}

		[DllImport("libpackagekit-glib.dll")]
		static extern unsafe int pk_control_get_network_state(IntPtr raw, out IntPtr error);

		public unsafe PackageKit.NetworkEnum GetNetworkState() {
			IntPtr error = IntPtr.Zero;
			int raw_ret = pk_control_get_network_state(Handle, out error);
			PackageKit.NetworkEnum ret = (PackageKit.NetworkEnum) raw_ret;
			if (error != IntPtr.Zero) throw new GLib.GException (error);
			return ret;
		}

		[DllImport("libpackagekit-glib.dll")]
		static extern unsafe bool pk_control_get_backend_detail(IntPtr raw, IntPtr name, IntPtr author, out IntPtr error);

		public unsafe bool GetBackendDetail(string name, string author) {
			IntPtr error = IntPtr.Zero;
			bool raw_ret = pk_control_get_backend_detail(Handle, GLib.Marshaller.StringToPtrGStrdup(name), GLib.Marshaller.StringToPtrGStrdup(author), out error);
			bool ret = raw_ret;
			if (error != IntPtr.Zero) throw new GLib.GException (error);
			return ret;
		}

		[DllImport("libpackagekit-glib.dll")]
		static extern int pk_control_error_quark();

		public static int ErrorQuark() {
			int raw_ret = pk_control_error_quark();
			int ret = raw_ret;
			return ret;
		}

		[DllImport("libpackagekit-glib.dll")]
		static extern unsafe bool pk_control_set_proxy(IntPtr raw, IntPtr proxy_http, IntPtr proxy_ftp, out IntPtr error);

		public unsafe bool SetProxy(string proxy_http, string proxy_ftp) {
			IntPtr native_proxy_http = GLib.Marshaller.StringToPtrGStrdup (proxy_http);
			IntPtr native_proxy_ftp = GLib.Marshaller.StringToPtrGStrdup (proxy_ftp);
			IntPtr error = IntPtr.Zero;
			bool raw_ret = pk_control_set_proxy(Handle, native_proxy_http, native_proxy_ftp, out error);
			bool ret = raw_ret;
			GLib.Marshaller.Free (native_proxy_http);
			GLib.Marshaller.Free (native_proxy_ftp);
			if (error != IntPtr.Zero) throw new GLib.GException (error);
			return ret;
		}

		[DllImport("libpackagekit-glib.dll")]
		static extern IntPtr pk_control_transaction_list_get(IntPtr raw);

		public string TransactionListGet() {
			IntPtr raw_ret = pk_control_transaction_list_get(Handle);
			string ret = GLib.Marshaller.Utf8PtrToString (raw_ret);
			return ret;
		}

		[DllImport("libpackagekit-glib.dll")]
		static extern bool pk_control_transaction_list_print(IntPtr raw);

		public bool TransactionListPrint() {
			bool raw_ret = pk_control_transaction_list_print(Handle);
			bool ret = raw_ret;
			return ret;
		}

		[DllImport("libpackagekit-glib.dll")]
		static extern unsafe ulong pk_control_get_filters(IntPtr raw, out IntPtr error);

		public unsafe ulong GetFilters() {
			IntPtr error = IntPtr.Zero;
			ulong raw_ret = pk_control_get_filters(Handle, out error);
			ulong ret = raw_ret;
			if (error != IntPtr.Zero) throw new GLib.GException (error);
			return ret;
		}

		[DllImport("libpackagekit-glib.dll")]
		static extern unsafe ulong pk_control_get_groups(IntPtr raw, out IntPtr error);

		public unsafe ulong GetGroups() {
			IntPtr error = IntPtr.Zero;
			ulong raw_ret = pk_control_get_groups(Handle, out error);
			ulong ret = raw_ret;
			if (error != IntPtr.Zero) throw new GLib.GException (error);
			return ret;
		}

		[DllImport("libpackagekit-glib.dll")]
		static extern IntPtr pk_control_get_type();

		public static new GLib.GType GType { 
			get {
				IntPtr raw_ret = pk_control_get_type();
				GLib.GType ret = new GLib.GType(raw_ret);
				return ret;
			}
		}


		static Control ()
		{
			GtkSharp.PackagekitSharp.ObjectManager.Initialize ();
		}
#endregion
	}
}
