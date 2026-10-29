/**
 * Çiğmamam Personel Giriş-Çıkış ve Mesai Takip Sistemi
 * Admin Yönetim Paneli - Uygulama Mantığı (app.js)
 */

// =============================================================================
// 1. FIREBASE KONFİGÜRASYONU VE BAŞLATMA
// =============================================================================

// Kullanıcı bu değerleri kendi Firebase projesinden dolduracaktır.
const firebaseConfig = {
  apiKey: "AIzaSyA_uB2Q4gtNO8wnuKawx_gfhiNVGquQ95s",
  authDomain: "cigmamam-personel.firebaseapp.com",
  projectId: "cigmamam-personel",
  storageBucket: "cigmamam-personel.firebasestorage.app",
  messagingSenderId: "648375549237",
  appId: "1:648375549237:web:7c20148f32bac4785be07c"
};

let app, auth, db;
let isFirebaseConfigured = false;

try {
  if (firebaseConfig.apiKey && firebaseConfig.apiKey !== "YOUR_API_KEY") {
    app = firebase.initializeApp(firebaseConfig);
    auth = firebase.auth();
    db = firebase.firestore();
    isFirebaseConfigured = true;
  } else {
    console.warn("[Firebase] Henüz geçerli bir firebaseConfig tanımlanmadı. Lütfen app.js dosyasındaki ayarları güncelleyin.");
  }
} catch (err) {
  console.error("[Firebase] Başlatma hatası:", err);
}

// =============================================================================
// 2. MERKEZİ DURUM YÖNETİMİ (STATE)
// =============================================================================

const state = {
  currentUser: null,
  currentTab: 'dashboard',
  employees: [],      // Firestore employees koleksiyonu
  events: [],         // Firestore events koleksiyonu
  settings: {
    shiftStartHour: 8,
    shiftStartMinute: 0,
    shiftEndHour: 17,
    shiftEndMinute: 0,
    adminUID: "00000000",
    deviceId: "ESP32_001"
  },
  // Kayıtlar sekmesi durumları
  records: {
    filteredList: [],
    currentPage: 1,
    pageSize: 20
  },
  // Dinleyici abonelikleri (unsubscribe fonksiyonları)
  subscriptions: {
    employees: null,
    events: null,
    settings: null
  }
};

// =============================================================================
// 3. YARDIMCI FONKSİYONLAR (HELPERS)
// =============================================================================

/**
 * Toast bildirimi gösterir
 * @param {string} message - Bildirim mesajı
 * @param {'success'|'error'|'warning'|'info'} type - Bildirim türü
 */
function showToast(message, type = 'info') {
  const container = document.getElementById('toast-container');
  if (!container) return;

  const toast = document.createElement('div');
  toast.className = `toast toast-${type}`;

  // İkon seçimi
  let iconSvg = '';
  if (type === 'success') {
    iconSvg = `<svg class="w-5 h-5 text-emerald-500 mr-2 flex-shrink-0" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M5 13l4 4L19 7"/></svg>`;
  } else if (type === 'error') {
    iconSvg = `<svg class="w-5 h-5 text-rose-500 mr-2 flex-shrink-0" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M6 18L18 6M6 6l12 12"/></svg>`;
  } else if (type === 'warning') {
    iconSvg = `<svg class="w-5 h-5 text-amber-500 mr-2 flex-shrink-0" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 9v2m0 4h.01m-6.938 4h13.856c1.54 0 2.502-1.667 1.732-3L13.732 4c-.77-1.333-2.694-1.333-3.464 0L3.34 16c-.77 1.333.192 3 1.732 3z"/></svg>`;
  } else {
    iconSvg = `<svg class="w-5 h-5 text-blue-500 mr-2 flex-shrink-0" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M13 16h-1v-4h-1m1-4h.01M21 12a9 9 0 11-18 0 9 9 0 0118 0z"/></svg>`;
  }

  toast.innerHTML = `
    ${iconSvg}
    <div class="flex-1 text-xs font-medium leading-relaxed">${escapeHtml(message)}</div>
    <button type="button" class="ml-2 text-slate-400 hover:text-slate-600 focus:outline-none">
      <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M6 18L18 6M6 6l12 12"/></svg>
    </button>
  `;

  // Kapat butonu olayı
  toast.querySelector('button').addEventListener('click', () => {
    dismissToast(toast);
  });

  container.appendChild(toast);

  // 4 saniye sonra otomatik kapat
  setTimeout(() => {
    dismissToast(toast);
  }, 4000);
}

function dismissToast(toast) {
  if (!toast || toast.classList.contains('toast-hiding')) return;
  toast.classList.add('toast-hiding');
  setTimeout(() => {
    if (toast.parentElement) toast.remove();
  }, 300);
}

function escapeHtml(str) {
  if (!str) return '';
  return String(str)
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;')
    .replace(/'/g, '&#039;');
}

/**
 * Unix zaman damgasını veya Date nesnesini 'DD.MM.YYYY HH:mm:ss' formatına çevirir
 */
function formatDateTime(val) {
  if (!val) return '-';
  let d;
  if (typeof val === 'number') {
    // Saniye mi milisaniye mi kontrolü
    d = val < 10000000000 ? new Date(val * 1000) : new Date(val);
  } else if (val.toDate && typeof val.toDate === 'function') {
    d = val.toDate();
  } else {
    d = new Date(val);
  }

  if (isNaN(d.getTime())) return '-';

  const day = String(d.getDate()).padStart(2, '0');
  const month = String(d.getMonth() + 1).padStart(2, '0');
  const year = d.getFullYear();
  const hours = String(d.getHours()).padStart(2, '0');
  const mins = String(d.getMinutes()).padStart(2, '0');
  const secs = String(d.getSeconds()).padStart(2, '0');

  return `${day}.${month}.${year} ${hours}:${mins}:${secs}`;
}

/**
 * Sadece saat ve dakika formatlar ('HH:mm')
 */
function formatTimeOnly(val) {
  if (!val) return '--:--';
  let d;
  if (typeof val === 'number') {
    d = val < 10000000000 ? new Date(val * 1000) : new Date(val);
  } else if (val.toDate && typeof val.toDate === 'function') {
    d = val.toDate();
  } else {
    d = new Date(val);
  }
  if (isNaN(d.getTime())) return '--:--';
  return `${String(d.getHours()).padStart(2, '0')}:${String(d.getMinutes()).padStart(2, '0')}`;
}

/**
 * Tarih formatlar ('DD.MM.YYYY')
 */
function formatDateOnly(val) {
  if (!val) return '--.--.----';
  let d;
  if (typeof val === 'number') {
    d = val < 10000000000 ? new Date(val * 1000) : new Date(val);
  } else if (typeof val === 'string' && val.includes('-')) {
    const parts = val.split('-');
    if (parts.length === 3) return `${parts[2]}.${parts[1]}.${parts[0]}`;
    d = new Date(val);
  } else {
    d = new Date(val);
  }
  if (isNaN(d.getTime())) return String(val);
  return `${String(d.getDate()).padStart(2, '0')}.${String(d.getMonth() + 1).padStart(2, '0')}.${d.getFullYear()}`;
}

/**
 * YYYY-MM-DD formatında bugünün yerel tarih stringini verir
 */
function getLocalDateString(d = new Date()) {
  const year = d.getFullYear();
  const month = String(d.getMonth() + 1).padStart(2, '0');
  const day = String(d.getDate()).padStart(2, '0');
  return `${year}-${month}-${day}`;
}

/**
 * UID formatlayıcı (İki karakterde bir iki nokta koyar)
 */
function formatUID(uid) {
  if (!uid) return '';
  const clean = uid.replace(/[^a-zA-Z0-9]/g, '').toUpperCase();
  const parts = clean.match(/.{1,2}/g);
  return parts ? parts.join(':') : clean;
}

/**
 * Güvenli Document ID (İki noktaları kaldırır)
 */
function toSafeDocId(uid) {
  if (!uid) return '';
  return uid.replace(/[^a-zA-Z0-9]/g, '').toUpperCase();
}

/**
 * Olay türüne göre Türkçe etiket ve renk sınıfı verir
 */
function getEventTypeInfo(type) {
  switch (type) {
    case 'GIRIS':
      return {
        label: 'Giriş',
        badgeClass: 'bg-emerald-50 text-emerald-700 border-emerald-200',
        dotColor: 'bg-emerald-500'
      };
    case 'CIKIS':
      return {
        label: 'Çıkış',
        badgeClass: 'bg-rose-50 text-rose-700 border-rose-200',
        dotColor: 'bg-rose-500'
      };
    case 'MOLA_BAS':
      return {
        label: 'Mola Başlangıç',
        badgeClass: 'bg-amber-50 text-amber-700 border-amber-200',
        dotColor: 'bg-amber-500'
      };
    case 'MOLA_BIT':
      return {
        label: 'Mola Bitiş',
        badgeClass: 'bg-blue-50 text-blue-700 border-blue-200',
        dotColor: 'bg-blue-500'
      };
    case 'OTO_CIKIS':
      return {
        label: 'Otomatik Çıkış',
        badgeClass: 'bg-purple-50 text-purple-700 border-purple-200',
        dotColor: 'bg-purple-500'
      };
    default:
      return {
        label: type || 'İşlem',
        badgeClass: 'bg-slate-50 text-slate-700 border-slate-200',
        dotColor: 'bg-slate-400'
      };
  }
}

/**
 * Personel çalışma durumuna göre Türkçe etiket ve rozet verir
 */
function getEmployeeStateInfo(stateCode) {
  const code = Number(stateCode);
  switch (code) {
    case 1:
      return {
        label: 'Çalışıyor',
        badgeClass: 'bg-emerald-50 text-emerald-700 border-emerald-200',
        dotColor: 'bg-emerald-500'
      };
    case 2:
      return {
        label: 'Molada',
        badgeClass: 'bg-amber-50 text-amber-700 border-amber-200',
        dotColor: 'bg-amber-500'
      };
    case 0:
    default:
      return {
        label: 'Dışarıda',
        badgeClass: 'bg-slate-100 text-slate-600 border-slate-200',
        dotColor: 'bg-slate-400'
      };
  }
}

// =============================================================================
// 4. KULLANICI ARAYÜZÜ ETKİLEŞİMİ (UI & MODALS)
// =============================================================================

function showLoader(text = 'İşlem yapılıyor...') {
  const loader = document.getElementById('global-loader');
  if (loader) {
    const textEl = loader.querySelector('p');
    if (textEl) textEl.textContent = text;
    loader.classList.remove('hidden');
    loader.style.opacity = '1';
  }
}

function hideLoader() {
  const loader = document.getElementById('global-loader');
  if (loader) {
    loader.style.opacity = '0';
    setTimeout(() => {
      loader.classList.add('hidden');
    }, 300);
  }
}

function openModal(modalId) {
  const modal = document.getElementById(modalId);
  if (!modal) return;
  modal.classList.remove('hidden');
  requestAnimationFrame(() => {
    modal.classList.add('show');
  });
}

function closeModal(modalId) {
  const modal = document.getElementById(modalId);
  if (!modal) return;
  modal.classList.remove('show');
  setTimeout(() => {
    modal.classList.add('hidden');
  }, 200);
}

// Modal kapatma butonları ve arka plan tıklaması dinleyicisi
document.querySelectorAll('.close-modal-btn').forEach(btn => {
  btn.addEventListener('click', () => {
    const backdrop = btn.closest('.modal-backdrop');
    if (backdrop) closeModal(backdrop.id);
  });
});

document.querySelectorAll('.modal-backdrop').forEach(modal => {
  modal.addEventListener('click', (e) => {
    if (e.target === modal) {
      closeModal(modal.id);
    }
  });
});

window.addEventListener('keydown', (e) => {
  if (e.key === 'Escape') {
    document.querySelectorAll('.modal-backdrop.show').forEach(m => closeModal(m.id));
  }
});

// Mobil menü geçişleri
const mobileSidebarToggle = document.getElementById('mobile-sidebar-toggle');
const sidebar = document.getElementById('sidebar');
const sidebarBackdrop = document.getElementById('sidebar-backdrop');
const closeSidebarBtn = document.getElementById('close-sidebar-btn');

function toggleMobileSidebar(open) {
  if (!sidebar || !sidebarBackdrop) return;
  if (open) {
    sidebar.classList.remove('-translate-x-full');
    sidebarBackdrop.classList.remove('hidden');
  } else {
    sidebar.classList.add('-translate-x-full');
    sidebarBackdrop.classList.add('hidden');
  }
}

if (mobileSidebarToggle) {
  mobileSidebarToggle.addEventListener('click', () => toggleMobileSidebar(true));
}
if (closeSidebarBtn) {
  closeSidebarBtn.addEventListener('click', () => toggleMobileSidebar(false));
}
if (sidebarBackdrop) {
  sidebarBackdrop.addEventListener('click', () => toggleMobileSidebar(false));
}

// Sekme Geçişi
function switchTab(tabId) {
  state.currentTab = tabId;

  // Navigasyon butonlarını güncelle
  document.querySelectorAll('.nav-tab').forEach(btn => {
    const isTarget = btn.getAttribute('data-tab') === tabId;
    if (isTarget) {
      btn.className = 'nav-tab active w-full flex items-center px-3.5 py-3 rounded-xl text-sm font-medium transition text-white bg-blue-600 shadow-md shadow-blue-600/20';
    } else {
      btn.className = 'nav-tab w-full flex items-center px-3.5 py-3 rounded-xl text-sm font-medium transition text-slate-400 hover:text-white hover:bg-slate-800';
    }
  });

  // İçerik panellerini güncelle
  document.querySelectorAll('.tab-content').forEach(section => {
    section.classList.remove('active');
  });

  const targetSection = document.getElementById(`tab-${tabId}`);
  if (targetSection) {
    targetSection.classList.add('active');
  }

  // Başlık metinlerini güncelle
  const pageTitle = document.getElementById('page-title');
  const pageSubtitle = document.getElementById('page-subtitle');

  switch (tabId) {
    case 'dashboard':
      pageTitle.textContent = 'Gösterge Paneli';
      pageSubtitle.textContent = 'Genel personel durumu ve anlık fabrika içi hareketler';
      renderDashboard();
      break;
    case 'employees':
      pageTitle.textContent = 'Personeller';
      pageSubtitle.textContent = 'Aktif kart kayıtları, isimler ve anlık çalışma durumları';
      renderEmployeesTable();
      break;
    case 'records':
      pageTitle.textContent = 'Giriş-Çıkış Kayıtları';
      pageSubtitle.textContent = 'Tüm terminal olaylarının detaylı dökümü ve filtreleme';
      initRecordsFilters();
      applyRecordsFilter();
      break;
    case 'reports':
      pageTitle.textContent = 'Mesai ve Devam Raporları';
      pageSubtitle.textContent = 'Geç kalma, erken çıkış ve mola sürelerinin analitik raporu';
      initReportDates();
      generateReport();
      break;
    case 'settings':
      pageTitle.textContent = 'Sistem Ayarları';
      pageSubtitle.textContent = 'Mesai saatleri, terminal donanım bilgisi ve yönetici şifresi';
      populateSettingsForm();
      break;
  }

  // Mobilde menüyü kapat
  toggleMobileSidebar(false);
}

document.querySelectorAll('.nav-tab').forEach(btn => {
  btn.addEventListener('click', () => {
    const tabId = btn.getAttribute('data-tab');
    if (tabId) switchTab(tabId);
  });
});

// Canlı Saat Sayacı
function updateLiveClock() {
  const now = new Date();
  const timeEl = document.getElementById('live-time');
  const dateEl = document.getElementById('live-date');

  if (timeEl) {
    timeEl.textContent = now.toLocaleTimeString('tr-TR', { hour12: false });
  }
  if (dateEl) {
    dateEl.textContent = now.toLocaleDateString('tr-TR', {
      weekday: 'long',
      day: 'numeric',
      month: 'long',
      year: 'numeric'
    });
  }
}
setInterval(updateLiveClock, 1000);
updateLiveClock();

// Şifre Göster/Gizle Butonu
const togglePasswordBtn = document.getElementById('toggle-password-btn');
const passwordInput = document.getElementById('login-password');
if (togglePasswordBtn && passwordInput) {
  togglePasswordBtn.addEventListener('click', () => {
    const isPassword = passwordInput.type === 'password';
    passwordInput.type = isPassword ? 'text' : 'password';
  });
}

// =============================================================================
// 5. KİMLİK DOĞRULAMA (FIREBASE AUTH)
// =============================================================================

function setupAuthListener() {
  if (!auth) {
    hideLoader();
    document.getElementById('login-view').classList.remove('hidden');
    showToast('Firebase konfigürasyonu tamamlanmadı. Lütfen app.js dosyasındaki firebaseConfig değerlerini girin.', 'warning');
    return;
  }

  auth.onAuthStateChanged((user) => {
    hideLoader();
    if (user) {
      state.currentUser = user;
      document.getElementById('login-view').classList.add('hidden');
      document.getElementById('app-view').classList.remove('hidden');

      // Kullanıcı arayüzü bilgileri
      const email = user.email || 'yonetici@cigmamam.com';
      document.getElementById('user-display-email').textContent = email;
      document.getElementById('settings-account-email').textContent = email;
      document.getElementById('user-avatar-initial').textContent = email.charAt(0).toUpperCase();

      // Gerçek zamanlı Firestore aboneliklerini başlat
      startFirestoreListeners();
      switchTab(state.currentTab || 'dashboard');
      showToast(`Hoş geldiniz, ${email}`, 'success');
    } else {
      state.currentUser = null;
      stopFirestoreListeners();
      document.getElementById('login-view').classList.remove('hidden');
      document.getElementById('app-view').classList.add('hidden');
    }
  });
}

// Giriş Formu Gönderimi
const loginForm = document.getElementById('login-form');
if (loginForm) {
  loginForm.addEventListener('submit', async (e) => {
    e.preventDefault();
    const email = document.getElementById('login-email').value.trim();
    const password = document.getElementById('login-password').value;
    const rememberMe = document.getElementById('remember-me').checked;
    const errorBox = document.getElementById('login-error');
    const errorText = document.getElementById('login-error-text');
    const btnSpinner = document.getElementById('login-btn-spinner');
    const submitBtn = document.getElementById('login-btn');

    if (!isFirebaseConfigured) {
      errorText.textContent = 'Firebase ayarları yapılandırılmamış. Lütfen app.js içindeki firebaseConfig nesnesini doldurunuz.';
      errorBox.classList.remove('hidden');
      return;
    }

    errorBox.classList.add('hidden');
    submitBtn.disabled = true;
    btnSpinner.classList.remove('hidden');

    try {
      const persistence = rememberMe
        ? firebase.auth.Auth.Persistence.LOCAL
        : firebase.auth.Auth.Persistence.SESSION;
      await auth.setPersistence(persistence);
      await auth.signInWithEmailAndPassword(email, password);
    } catch (err) {
      console.error('[Auth Hatası]', err);
      let message = 'Giriş yapılamadı. Lütfen e-posta ve şifrenizi kontrol edin.';
      if (err.code === 'auth/user-not-found' || err.code === 'auth/wrong-password' || err.code === 'auth/invalid-credential') {
        message = 'Hatalı e-posta adresi veya şifre girdiniz.';
      } else if (err.code === 'auth/invalid-email') {
        message = 'Geçersiz e-posta adresi formatı.';
      } else if (err.code === 'auth/too-many-requests') {
        message = 'Çok fazla başarısız deneme. Lütfen birkaç dakika sonra tekrar deneyin.';
      } else if (err.code === 'auth/network-request-failed') {
        message = 'Ağ bağlantı hatası. İnternet bağlantınızı kontrol edin.';
      }
      errorText.textContent = message;
      errorBox.classList.remove('hidden');
    } finally {
      submitBtn.disabled = false;
      btnSpinner.classList.remove('hidden');
      btnSpinner.classList.add('hidden');
    }
  });
}

// Çıkış Yap Butonları
function handleLogout() {
  if (!auth) return;
  auth.signOut()
    .then(() => {
      showToast('Oturum başarıyla kapatıldı.', 'info');
    })
    .catch((err) => {
      showToast('Çıkış yapılırken bir hata oluştu: ' + err.message, 'error');
    });
}

document.getElementById('logout-btn')?.addEventListener('click', handleLogout);
document.getElementById('settings-logout-btn')?.addEventListener('click', handleLogout);

// Şifre Değiştirme
const settingsPasswordForm = document.getElementById('settings-password-form');
if (settingsPasswordForm) {
  settingsPasswordForm.addEventListener('submit', async (e) => {
    e.preventDefault();
    const newPass = document.getElementById('settings-new-password').value;
    const confirmPass = document.getElementById('settings-confirm-password').value;
    const spinner = document.getElementById('change-pwd-spinner');
    const submitBtn = document.getElementById('change-password-btn');

    if (newPass !== confirmPass) {
      showToast('Girdiğiniz şifreler birbiriyle eşleşmiyor!', 'warning');
      return;
    }

    if (newPass.length < 6) {
      showToast('Şifre en az 6 karakter olmalıdır.', 'warning');
      return;
    }

    if (!auth || !auth.currentUser) return;

    submitBtn.disabled = true;
    spinner.classList.remove('hidden');

    try {
      await auth.currentUser.updatePassword(newPass);
      showToast('Yönetici şifreniz başarıyla güncellendi.', 'success');
      settingsPasswordForm.reset();
    } catch (err) {
      console.error('[Şifre Güncelleme Hatası]', err);
      if (err.code === 'auth/requires-recent-login') {
        showToast('Güvenlik nedeniyle şifre değiştirmeden önce tekrar giriş yapmalısınız.', 'warning');
      } else {
        showToast('Şifre güncellenemedi: ' + err.message, 'error');
      }
    } finally {
      submitBtn.disabled = false;
      spinner.classList.add('hidden');
    }
  });
}

// =============================================================================
// 6. FIRESTORE GERÇEK ZAMANLI VERİ DİNLEYİCİLERİ
// =============================================================================

function startFirestoreListeners() {
  if (!db) return;

  // 1. Ayarlar Dokümanı Dinleyicisi (settings/main)
  state.subscriptions.settings = db.collection('settings').doc('main')
    .onSnapshot((doc) => {
      if (doc.exists) {
        const data = doc.data();
        state.settings = {
          shiftStartHour: data.shiftStartHour !== undefined ? Number(data.shiftStartHour) : 8,
          shiftStartMinute: data.shiftStartMinute !== undefined ? Number(data.shiftStartMinute) : 0,
          shiftEndHour: data.shiftEndHour !== undefined ? Number(data.shiftEndHour) : 17,
          shiftEndMinute: data.shiftEndMinute !== undefined ? Number(data.shiftEndMinute) : 0,
          adminUID: data.adminUID || "00000000",
          deviceId: data.deviceId || "ESP32_001"
        };
      } else {
        // Varsayılan ayarları oluştur
        initDefaultSettingsDoc();
      }
      updateShiftBadges();
      if (state.currentTab === 'settings') populateSettingsForm();
      if (state.currentTab === 'dashboard') renderDashboard();
    }, (err) => {
      console.error('[Firestore settings dinleme hatası]', err);
    });

  // 2. Personeller Koleksiyonu Dinleyicisi (employees)
  state.subscriptions.employees = db.collection('employees')
    .onSnapshot((snapshot) => {
      const list = [];
      snapshot.forEach(doc => {
        const data = doc.data();
        list.push({
          id: doc.id,
          uid: data.uid || doc.id,
          name: data.name || 'İsimsiz Personel',
          state: data.state !== undefined ? Number(data.state) : 0,
          active: data.active !== undefined ? Boolean(data.active) : true,
          lastUpdate: data.lastUpdate || 0,
          createdAt: data.createdAt || null
        });
      });

      // İsim alfabetik sıralama
      list.sort((a, b) => a.name.localeCompare(b.name, 'tr-TR'));
      state.employees = list;

      // Navigasyon sayısını güncelle
      const activeCount = list.filter(e => e.active).length;
      const navEmpCount = document.getElementById('nav-emp-count');
      if (navEmpCount) navEmpCount.textContent = activeCount;

      // Kayıtlar personeller filtresini güncelle
      populateEmployeeFilterDropdown();

      // Aktif sekmeye göre render et
      if (state.currentTab === 'dashboard') renderDashboard();
      if (state.currentTab === 'employees') renderEmployeesTable();
      if (state.currentTab === 'records') applyRecordsFilter();
      if (state.currentTab === 'reports') generateReport();
    }, (err) => {
      console.error('[Firestore employees dinleme hatası]', err);
      showToast('Personel listesi güncellenirken hata oluştu.', 'error');
    });

  // 3. Olaylar Koleksiyonu Dinleyicisi (events)
  // Son 2000 olayı dinle (zamana göre azalan)
  state.subscriptions.events = db.collection('events')
    .orderBy('timestamp', 'desc')
    .limit(2000)
    .onSnapshot((snapshot) => {
      const list = [];
      snapshot.forEach(doc => {
        const data = doc.data();
        list.push({
          id: doc.id,
          uid: data.uid || '',
          employeeName: data.employeeName || 'Bilinmiyor',
          type: data.type || '',
          timestamp: data.timestamp !== undefined ? Number(data.timestamp) : 0,
          date: data.date || '',
          deviceId: data.deviceId || 'ESP32_001',
          localId: data.localId || 0
        });
      });

      state.events = list;

      // OTOMATİK PERSONEL SENKRONİZASYONU:
      // Eğer bir personel cihazdan hızlıca eklenip kart bastıysa ancak Firestore 'employees'
      // koleksiyonuna henüz yazılmamışsa, gelen events kayıtlarından bunu anında yakala ve employees'e ekle!
      syncMissingEmployeesFromEvents(list);

      if (state.currentTab === 'dashboard') renderDashboard();
      if (state.currentTab === 'records') applyRecordsFilter();
      if (state.currentTab === 'reports') generateReport();
    }, (err) => {
      console.error('[Firestore events dinleme hatası]', err);
    });
}

function stopFirestoreListeners() {
  if (state.subscriptions.settings) {
    state.subscriptions.settings();
    state.subscriptions.settings = null;
  }
  if (state.subscriptions.employees) {
    state.subscriptions.employees();
    state.subscriptions.employees = null;
  }
  if (state.subscriptions.events) {
    state.subscriptions.events();
    state.subscriptions.events = null;
  }
}

/**
 * Gelen giriş-çıkış olaylarından henüz employees koleksiyonunda olmayan personelleri otomatik kurtarır/oluşturur.
 */
async function syncMissingEmployeesFromEvents(eventsList) {
  if (!db || !eventsList || eventsList.length === 0) return;

  const existingUids = new Set((state.employees || []).map(e => e.uid));
  const seenInBatch = new Set();

  for (const evt of eventsList) {
    if (!evt.uid || existingUids.has(evt.uid) || seenInBatch.has(evt.uid)) continue;
    seenInBatch.add(evt.uid);

    const safeDocId = evt.uid.replace(/:/g, '');
    let empState = 0;
    if (evt.type === 'GIRIS' || evt.type === 'MOLA_BIT') empState = 1;
    else if (evt.type === 'MOLA_BAS') empState = 2;
    else if (evt.type === 'CIKIS' || evt.type === 'OTO_CIKIS') empState = 0;

    try {
      await db.collection('employees').doc(safeDocId).set({
        uid: evt.uid,
        name: evt.employeeName || `Personel_${evt.uid.slice(-5)}`,
        state: empState,
        active: true,
        lastUpdate: evt.timestamp || Math.floor(Date.now() / 1000),
        createdAt: firebase.firestore.FieldValue.serverTimestamp()
      }, { merge: true });
      console.log(`[Otomatik Eşitleme] Eksik personel employees koleksiyonuna eklendi: ${evt.uid} (${evt.employeeName})`);
    } catch (err) {
      console.error('[Otomatik personel ekleme hatası]', err);
    }
  }
}

async function initDefaultSettingsDoc() {
  if (!db) return;
  try {
    await db.collection('settings').doc('main').set({
      shiftStartHour: 8,
      shiftStartMinute: 0,
      shiftEndHour: 17,
      shiftEndMinute: 0,
      adminUID: "00000000",
      deviceId: "ESP32_001",
      updatedAt: firebase.firestore.FieldValue.serverTimestamp()
    }, { merge: true });
  } catch (err) {
    console.error('[Varsayılan ayarlar oluşturulamadı]', err);
  }
}

function updateShiftBadges() {
  const startStr = `${String(state.settings.shiftStartHour).padStart(2, '0')}:${String(state.settings.shiftStartMinute).padStart(2, '0')}`;
  const endStr = `${String(state.settings.shiftEndHour).padStart(2, '0')}:${String(state.settings.shiftEndMinute).padStart(2, '0')}`;
  
  const badge = document.getElementById('shift-summary-badge');
  if (badge) {
    badge.textContent = `Mesai: ${startStr} - ${endStr}`;
  }

  const sidebarDevice = document.getElementById('sidebar-device-id');
  if (sidebarDevice) {
    sidebarDevice.textContent = `Cihaz: ${state.settings.deviceId || 'ESP32_001'}`;
  }
}

// =============================================================================
// 7. TAB 1: GÖSTERGE PANELİ (DASHBOARD)
// =============================================================================

function renderDashboard() {
  const activeEmployees = state.employees.filter(e => e.active);
  const totalCount = activeEmployees.length;

  // Şu an içeride olanlar: state === 1 (Çalışıyor) veya state === 2 (Molada)
  const workingEmployees = activeEmployees.filter(e => e.state === 1);
  const onBreakEmployees = activeEmployees.filter(e => e.state === 2);
  const presentEmployees = [...workingEmployees, ...onBreakEmployees];

  // Bugün geç kalanlar hesaplaması
  const todayStr = getLocalDateString();
  const shiftStartMinutes = (state.settings.shiftStartHour * 60) + state.settings.shiftStartMinute;

  // Bugünün tüm olaylarını al
  const todayEvents = state.events.filter(evt => {
    if (evt.date && evt.date === todayStr) return true;
    if (evt.timestamp) {
      const evtDate = getLocalDateString(new Date(evt.timestamp * 1000));
      return evtDate === todayStr;
    }
    return false;
  });

  // Her personel için bugünün ilk GIRIS olayını bul
  const firstGirisByUid = {};
  todayEvents.forEach(evt => {
    if (evt.type === 'GIRIS' && evt.timestamp) {
      if (!firstGirisByUid[evt.uid] || evt.timestamp < firstGirisByUid[evt.uid].timestamp) {
        firstGirisByUid[evt.uid] = evt;
      }
    }
  });

  let lateCount = 0;
  Object.values(firstGirisByUid).forEach(evt => {
    const d = new Date(evt.timestamp * 1000);
    const eventMinutes = (d.getHours() * 60) + d.getMinutes();
    if (eventMinutes > shiftStartMinutes) {
      lateCount++;
    }
  });

  // Stat Kartlarını Güncelle
  document.getElementById('stat-total-employees').textContent = totalCount;
  document.getElementById('stat-present-now').textContent = presentEmployees.length;
  document.getElementById('stat-on-break').textContent = onBreakEmployees.length;
  document.getElementById('stat-late-arrivals').textContent = lateCount;

  // Şu An İçeride Olan Personeller Listesi
  const presentListEl = document.getElementById('present-employees-list');
  const presentCountBadge = document.getElementById('present-count-badge');
  presentCountBadge.textContent = `${presentEmployees.length} Kişi`;

  if (presentEmployees.length === 0) {
    presentListEl.innerHTML = `
      <div class="py-10 text-center text-slate-400">
        <svg class="w-8 h-8 mx-auto mb-2 opacity-50" fill="none" stroke="currentColor" viewBox="0 0 24 24">
          <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M20 12H4"/>
        </svg>
        <p class="text-xs">Şu anda içeride personel bulunmuyor.</p>
      </div>
    `;
  } else {
    presentListEl.innerHTML = presentEmployees.map(emp => {
      const stateInfo = getEmployeeStateInfo(emp.state);
      const lastActionTime = emp.lastUpdate ? formatTimeOnly(emp.lastUpdate) : '--:--';

      return `
        <div class="flex items-center justify-between p-3 hover:bg-slate-50 transition rounded-xl">
          <div class="flex items-center space-x-3 min-w-0">
            <span class="w-2.5 h-2.5 rounded-full ${stateInfo.dotColor} flex-shrink-0"></span>
            <div class="min-w-0">
              <p class="text-sm font-semibold text-slate-800 truncate">${escapeHtml(emp.name)}</p>
              <p class="text-[11px] font-mono text-slate-400">${escapeHtml(emp.uid)}</p>
            </div>
          </div>
          <div class="flex items-center space-x-2 text-right">
            <span class="text-[11px] text-slate-400 tabular-nums">Son: ${lastActionTime}</span>
            <span class="text-xs px-2.5 py-0.5 rounded-full font-medium border ${stateInfo.badgeClass}">
              ${stateInfo.label}
            </span>
          </div>
        </div>
      `;
    }).join('');
  }

  // Bugünün Aktivite Akışı (Son 20 Olay)
  const activityFeedEl = document.getElementById('today-activity-feed');
  if (todayEvents.length === 0) {
    activityFeedEl.innerHTML = `
      <div class="py-10 text-center text-slate-400">
        <svg class="w-8 h-8 mx-auto mb-2 opacity-50" fill="none" stroke="currentColor" viewBox="0 0 24 24">
          <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 8v4l3 3m6-3a9 9 0 11-18 0 9 9 0 0118 0z"/>
        </svg>
        <p class="text-xs">Bugün için henüz bir giriş-çıkış hareketi yok.</p>
      </div>
    `;
  } else {
    // En yeni ilk gelecek şekilde sırala
    const sortedToday = [...todayEvents].sort((a, b) => (b.timestamp || 0) - (a.timestamp || 0)).slice(0, 20);

    activityFeedEl.innerHTML = sortedToday.map(evt => {
      const typeInfo = getEventTypeInfo(evt.type);
      const timeStr = formatTimeOnly(evt.timestamp);

      return `
        <div class="flex items-center justify-between p-3 hover:bg-slate-50 transition rounded-xl">
          <div class="flex items-center space-x-3 min-w-0">
            <span class="w-2 h-2 rounded-full ${typeInfo.dotColor} flex-shrink-0"></span>
            <div class="min-w-0">
              <p class="text-sm font-medium text-slate-800 truncate">${escapeHtml(evt.employeeName)}</p>
              <p class="text-[11px] text-slate-400 flex items-center space-x-1">
                <span>Cihaz: ${escapeHtml(evt.deviceId || 'ESP32')}</span>
                <span>&bull;</span>
                <span class="font-mono">${escapeHtml(evt.uid)}</span>
              </p>
            </div>
          </div>
          <div class="flex items-center space-x-2.5 text-right">
            <span class="text-xs font-semibold text-slate-600 tabular-nums">${timeStr}</span>
            <span class="text-xs px-2.5 py-0.5 rounded-full font-medium border ${typeInfo.badgeClass}">
              ${typeInfo.label}
            </span>
          </div>
        </div>
      `;
    }).join('');
  }
}

// =============================================================================
// 8. TAB 2: PERSONELLER (EMPLOYEES)
// =============================================================================

function renderEmployeesTable() {
  const tableBody = document.getElementById('employees-table-body');
  const emptyState = document.getElementById('employees-empty-state');
  const searchInput = document.getElementById('employee-search-input');
  const statusFilter = document.getElementById('employee-status-filter');

  const searchTerm = searchInput ? searchInput.value.trim().toLowerCase() : '';
  const statusTerm = statusFilter ? statusFilter.value : 'ALL';

  // Sadece aktif olanları listele (silinmemiş olanlar)
  let list = state.employees.filter(e => e.active);

  // Filtreler
  if (searchTerm) {
    list = list.filter(e => 
      e.name.toLowerCase().includes(searchTerm) || 
      e.uid.toLowerCase().includes(searchTerm)
    );
  }

  if (statusTerm !== 'ALL') {
    const code = Number(statusTerm);
    list = list.filter(e => e.state === code);
  }

  if (list.length === 0) {
    tableBody.innerHTML = '';
    emptyState.classList.remove('hidden');
    return;
  }

  emptyState.classList.add('hidden');

  tableBody.innerHTML = list.map((emp, index) => {
    const stateInfo = getEmployeeStateInfo(emp.state);
    const lastTime = emp.lastUpdate ? formatDateTime(emp.lastUpdate) : '-';

    return `
      <tr class="hover:bg-slate-50 transition">
        <td class="py-3.5 px-4 text-center text-xs text-slate-400 font-semibold">${index + 1}</td>
        <td class="py-3.5 px-4 font-semibold text-slate-900">${escapeHtml(emp.name)}</td>
        <td class="py-3.5 px-4 font-mono text-xs text-slate-600">${escapeHtml(emp.uid)}</td>
        <td class="py-3.5 px-4">
          <span class="inline-flex items-center text-xs px-2.5 py-0.5 rounded-full font-medium border ${stateInfo.badgeClass}">
            <span class="w-1.5 h-1.5 rounded-full ${stateInfo.dotColor} mr-1.5"></span>
            ${stateInfo.label}
          </span>
        </td>
        <td class="py-3.5 px-4 text-xs text-slate-500 tabular-nums">${lastTime}</td>
        <td class="py-3.5 px-4 text-right space-x-1">
          <button onclick="handleOpenEditModal('${emp.id}', '${encodeURIComponent(emp.name)}', '${encodeURIComponent(emp.uid)}')" 
            class="p-1.5 text-slate-500 hover:text-blue-600 hover:bg-blue-50 rounded-lg transition" title="Düzenle">
            <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M11 5H6a2 2 0 00-2 2v11a2 2 0 002 2h11a2 2 0 002-2v-5m-1.414-9.414a2 2 0 112.828 2.828L11.828 15H9v-2.828l8.586-8.586z"/>
            </svg>
          </button>
          <button onclick="handleOpenDeleteModal('${emp.id}', '${encodeURIComponent(emp.name)}')" 
            class="p-1.5 text-slate-500 hover:text-rose-600 hover:bg-rose-50 rounded-lg transition" title="Sil">
            <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M19 7l-.867 12.142A2 2 0 0116.138 21H7.862a2 2 0 01-1.995-1.858L5 7m5 4v6m4-6v6m1-10V4a1 1 0 00-1-1h-4a1 1 0 00-1 1v3M4 7h16"/>
            </svg>
          </button>
        </td>
      </tr>
    `;
  }).join('');
}

// Arama & Filtre dinleyicileri
document.getElementById('employee-search-input')?.addEventListener('input', renderEmployeesTable);
document.getElementById('employee-status-filter')?.addEventListener('change', renderEmployeesTable);

// 1. Yeni Personel Ekle
document.getElementById('open-add-employee-modal')?.addEventListener('click', () => {
  document.getElementById('form-add-employee').reset();
  openModal('modal-add-employee');
});

const formAddEmployee = document.getElementById('form-add-employee');
if (formAddEmployee) {
  formAddEmployee.addEventListener('submit', async (e) => {
    e.preventDefault();
    const nameInput = document.getElementById('add-emp-name').value.trim();
    const uidRaw = document.getElementById('add-emp-uid').value.trim();
    const spinner = document.getElementById('spinner-add-emp');
    const submitBtn = document.getElementById('btn-save-add-employee');

    if (!nameInput || !uidRaw) {
      showToast('Lütfen ad soyad ve kart UID alanlarını doldurun.', 'warning');
      return;
    }

    if (!db) {
      showToast('Firebase veritabanı hazır değil.', 'error');
      return;
    }

    // UID biçimlendir: docId = UID colonsuz (örn: AABBCCDD)
    const docId = toSafeDocId(uidRaw);
    const formattedUid = formatUID(uidRaw);

    if (docId.length < 4) {
      showToast('Geçersiz NFC UID formatı.', 'warning');
      return;
    }

    submitBtn.disabled = true;
    spinner.classList.remove('hidden');

    try {
      // Belgeyi Firestore employees koleksiyonuna yaz
      // active=true, state=0 (Dışarıda)
      await db.collection('employees').doc(docId).set({
        uid: formattedUid,
        name: nameInput,
        state: 0,
        active: true,
        lastUpdate: Math.floor(Date.now() / 1000),
        createdAt: firebase.firestore.FieldValue.serverTimestamp()
      }, { merge: true });

      showToast(`'${nameInput}' başarıyla eklendi. ESP32 terminali bir sonraki senkronizasyonda bu personeli indirecektir.`, 'success');
      closeModal('modal-add-employee');
      formAddEmployee.reset();
    } catch (err) {
      console.error('[Personel Ekleme Hatası]', err);
      showToast('Personel eklenirken hata oluştu: ' + err.message, 'error');
    } finally {
      submitBtn.disabled = false;
      spinner.classList.add('hidden');
    }
  });
}

// 2. Personel Düzenle
window.handleOpenEditModal = function(docId, encodedName, encodedUid) {
  const name = decodeURIComponent(encodedName);
  const uid = decodeURIComponent(encodedUid);

  document.getElementById('edit-emp-doc-id').value = docId;
  document.getElementById('edit-emp-uid').value = uid;
  document.getElementById('edit-emp-name').value = name;

  openModal('modal-edit-employee');
};

const formEditEmployee = document.getElementById('form-edit-employee');
if (formEditEmployee) {
  formEditEmployee.addEventListener('submit', async (e) => {
    e.preventDefault();
    const docId = document.getElementById('edit-emp-doc-id').value;
    const newName = document.getElementById('edit-emp-name').value.trim();
    const spinner = document.getElementById('spinner-edit-emp');
    const submitBtn = document.getElementById('btn-save-edit-employee');

    if (!newName) {
      showToast('Personel ismi boş bırakılamaz.', 'warning');
      return;
    }

    if (!db || !docId) return;

    submitBtn.disabled = true;
    spinner.classList.remove('hidden');

    try {
      await db.collection('employees').doc(docId).update({
        name: newName,
        lastUpdate: Math.floor(Date.now() / 1000)
      });

      showToast('Personel bilgisi güncellendi.', 'success');
      closeModal('modal-edit-employee');
    } catch (err) {
      console.error('[Personel Güncelleme Hatası]', err);
      showToast('Güncelleme sırasında hata oluştu: ' + err.message, 'error');
    } finally {
      submitBtn.disabled = false;
      spinner.classList.add('hidden');
    }
  });
}

// 3. Personel Sil
window.handleOpenDeleteModal = function(docId, encodedName) {
  const name = decodeURIComponent(encodedName);
  document.getElementById('delete-emp-doc-id').value = docId;
  document.getElementById('delete-emp-name').textContent = name;
  openModal('modal-delete-employee');
};

document.getElementById('btn-confirm-delete-employee')?.addEventListener('click', async () => {
  const docId = document.getElementById('delete-emp-doc-id').value;
  const spinner = document.getElementById('spinner-delete-emp');
  const submitBtn = document.getElementById('btn-confirm-delete-employee');

  if (!db || !docId) return;

  submitBtn.disabled = true;
  spinner.classList.remove('hidden');

  try {
    // ESP32 firmware, active=false olanları indirdiğinde yerel LittleFS hafızasından siler:
    // else if (idx >= 0 && !active) { employeeRemove(idx); }
    await db.collection('employees').doc(docId).update({
      active: false,
      state: 0,
      lastUpdate: Math.floor(Date.now() / 1000)
    });

    showToast('Personel pasife alındı ve silindi.', 'success');
    closeModal('modal-delete-employee');
  } catch (err) {
    console.error('[Personel Silme Hatası]', err);
    showToast('Silme sırasında hata oluştu: ' + err.message, 'error');
  } finally {
    submitBtn.disabled = false;
    spinner.classList.add('hidden');
  }
});

// =============================================================================
// 9. TAB 3: KAYITLAR (RECORDS)
// =============================================================================

function populateEmployeeFilterDropdown() {
  const select = document.getElementById('record-filter-employee');
  if (!select) return;

  const currentVal = select.value;
  let options = '<option value="ALL">Tüm Personeller</option>';

  state.employees.forEach(emp => {
    options += `<option value="${escapeHtml(emp.uid)}">${escapeHtml(emp.name)} (${escapeHtml(emp.uid)})</option>`;
  });

  select.innerHTML = options;
  if (currentVal) select.value = currentVal;
}

function initRecordsFilters() {
  const startInput = document.getElementById('record-filter-start');
  const endInput = document.getElementById('record-filter-end');

  // Varsayılan: Ayın başından bugüne
  if (!startInput.value) {
    const d = new Date();
    d.setDate(1); // Ayın 1'i
    startInput.value = getLocalDateString(d);
  }
  if (!endInput.value) {
    endInput.value = getLocalDateString(new Date());
  }
}

function applyRecordsFilter() {
  const startDateStr = document.getElementById('record-filter-start')?.value || '';
  const endDateStr = document.getElementById('record-filter-end')?.value || '';
  const selectedEmpUid = document.getElementById('record-filter-employee')?.value || 'ALL';

  // Seçili olay türleri
  const checkedTypes = [];
  document.querySelectorAll('input[name="record-event-type"]:checked').forEach(cb => {
    checkedTypes.push(cb.value);
  });

  let list = [...state.events];

  // Tarih aralığı filtresi
  if (startDateStr) {
    list = list.filter(evt => {
      const evtDate = evt.date || (evt.timestamp ? getLocalDateString(new Date(evt.timestamp * 1000)) : '');
      return evtDate >= startDateStr;
    });
  }

  if (endDateStr) {
    list = list.filter(evt => {
      const evtDate = evt.date || (evt.timestamp ? getLocalDateString(new Date(evt.timestamp * 1000)) : '');
      return evtDate <= endDateStr;
    });
  }

  // Personel filtresi
  if (selectedEmpUid !== 'ALL') {
    list = list.filter(evt => evt.uid === selectedEmpUid);
  }

  // İşlem türü filtresi
  if (checkedTypes.length > 0) {
    list = list.filter(evt => checkedTypes.includes(evt.type));
  } else {
    list = []; // Hiçbir onay kutusu seçili değilse boş dön
  }

  state.records.filteredList = list;
  state.records.currentPage = 1;
  renderRecordsTable();
}

function renderRecordsTable() {
  const tableBody = document.getElementById('records-table-body');
  const emptyState = document.getElementById('records-empty-state');
  const countSummary = document.getElementById('records-count-summary');
  const pageInfo = document.getElementById('records-page-info');
  const prevBtn = document.getElementById('records-prev-page');
  const nextBtn = document.getElementById('records-next-page');
  const pageNumbersEl = document.getElementById('records-page-numbers');

  const list = state.records.filteredList;
  const total = list.length;
  countSummary.textContent = `Toplam ${total} kayıt listeleniyor`;

  if (total === 0) {
    tableBody.innerHTML = '';
    emptyState.classList.remove('hidden');
    pageInfo.textContent = 'Sayfa 0 / 0 (Toplam 0 kayıt)';
    prevBtn.disabled = true;
    nextBtn.disabled = true;
    pageNumbersEl.innerHTML = '';
    return;
  }

  emptyState.classList.add('hidden');

  const pageSize = state.records.pageSize;
  const totalPages = Math.ceil(total / pageSize);
  const curPage = Math.min(Math.max(1, state.records.currentPage), totalPages);
  state.records.currentPage = curPage;

  const startIndex = (curPage - 1) * pageSize;
  const pagedItems = list.slice(startIndex, startIndex + pageSize);

  // Tablo satırlarını oluştur
  tableBody.innerHTML = pagedItems.map(evt => {
    const typeInfo = getEventTypeInfo(evt.type);
    const dateFormatted = evt.date ? formatDateOnly(evt.date) : formatDateOnly(evt.timestamp);
    const timeFormatted = formatTimeOnly(evt.timestamp);

    return `
      <tr class="hover:bg-slate-50 transition">
        <td class="py-3 px-4 text-xs font-semibold text-slate-800 tabular-nums">${dateFormatted}</td>
        <td class="py-3 px-4 text-xs font-semibold text-blue-700 tabular-nums">${timeFormatted}</td>
        <td class="py-3 px-4 font-semibold text-slate-900">${escapeHtml(evt.employeeName)}</td>
        <td class="py-3 px-4 font-mono text-xs text-slate-500">${escapeHtml(evt.uid)}</td>
        <td class="py-3 px-4">
          <span class="inline-flex items-center text-xs px-2.5 py-0.5 rounded-full font-medium border ${typeInfo.badgeClass}">
            <span class="w-1.5 h-1.5 rounded-full ${typeInfo.dotColor} mr-1.5"></span>
            ${typeInfo.label}
          </span>
        </td>
        <td class="py-3 px-4 text-xs font-mono text-slate-500">${escapeHtml(evt.deviceId || 'ESP32')}</td>
      </tr>
    `;
  }).join('');

  // Sayfalama kontrollerini güncelle
  pageInfo.textContent = `Sayfa ${curPage} / ${totalPages} (Toplam ${total} kayıt)`;
  prevBtn.disabled = curPage <= 1;
  nextBtn.disabled = curPage >= totalPages;

  // Sayfa numarası butonları
  let pageButtonsHtml = '';
  const maxButtons = 5;
  let startBtn = Math.max(1, curPage - Math.floor(maxButtons / 2));
  let endBtn = Math.min(totalPages, startBtn + maxButtons - 1);
  if (endBtn - startBtn + 1 < maxButtons) {
    startBtn = Math.max(1, endBtn - maxButtons + 1);
  }

  for (let i = startBtn; i <= endBtn; i++) {
    const isActive = i === curPage;
    pageButtonsHtml += `
      <button onclick="goToRecordsPage(${i})" 
        class="w-7 h-7 rounded-lg text-xs font-medium transition ${isActive ? 'bg-blue-600 text-white shadow-sm' : 'border border-slate-200 bg-white text-slate-600 hover:bg-slate-50'}">
        ${i}
      </button>
    `;
  }
  pageNumbersEl.innerHTML = pageButtonsHtml;
}

window.goToRecordsPage = function(pageNum) {
  state.records.currentPage = pageNum;
  renderRecordsTable();
};

document.getElementById('records-prev-page')?.addEventListener('click', () => {
  if (state.records.currentPage > 1) {
    state.records.currentPage--;
    renderRecordsTable();
  }
});

document.getElementById('records-next-page')?.addEventListener('click', () => {
  state.records.currentPage++;
  renderRecordsTable();
});

document.getElementById('records-filter-apply-btn')?.addEventListener('click', applyRecordsFilter);
document.getElementById('records-filter-reset-btn')?.addEventListener('click', () => {
  initRecordsFilters();
  const empSelect = document.getElementById('record-filter-employee');
  if (empSelect) empSelect.value = 'ALL';
  document.querySelectorAll('input[name="record-event-type"]').forEach(cb => {
    cb.checked = true;
  });
  applyRecordsFilter();
});

// CSV Dışa Aktarma
document.getElementById('records-export-csv-btn')?.addEventListener('click', () => {
  const list = state.records.filteredList;
  if (!list || list.length === 0) {
    showToast('Dışa aktarılacak kayıt bulunmuyor.', 'warning');
    return;
  }

  let csvContent = '\uFEFF'; // Excel için Türkçe karakter BOM
  csvContent += 'Tarih;Saat;Personel;Kart UID;İşlem Türü;Cihaz;Olay ID\r\n';

  list.forEach(evt => {
    const typeInfo = getEventTypeInfo(evt.type);
    const dateFormatted = evt.date ? formatDateOnly(evt.date) : formatDateOnly(evt.timestamp);
    const timeFormatted = formatTimeOnly(evt.timestamp);
    const name = (evt.employeeName || '').replace(/;/g, ',');
    const uid = (evt.uid || '').replace(/;/g, ',');
    const type = typeInfo.label.replace(/;/g, ',');
    const device = (evt.deviceId || 'ESP32_001').replace(/;/g, ',');
    const id = evt.localId || evt.id;

    csvContent += `"${dateFormatted}";"${timeFormatted}";"${name}";"${uid}";"${type}";"${device}";"${id}"\r\n`;
  });

  const blob = new Blob([csvContent], { type: 'text/csv;charset=utf-8;' });
  const url = URL.createObjectURL(blob);
  const link = document.createElement('a');
  link.setAttribute('href', url);
  const nowStr = getLocalDateString();
  link.setAttribute('download', `cigmamam_kayitlar_${nowStr}.csv`);
  document.body.appendChild(link);
  link.click();
  document.body.removeChild(link);
  URL.revokeObjectURL(url);
  showToast('Kayıtlar CSV dosyası olarak indirildi.', 'success');
});

// =============================================================================
// 10. TAB 4: RAPORLAR (REPORTS)
// =============================================================================

function initReportDates() {
  const startInput = document.getElementById('report-start-date');
  const endInput = document.getElementById('report-end-date');

  if (!startInput.value) {
    const d = new Date();
    d.setDate(1); // Ayın 1'i
    startInput.value = getLocalDateString(d);
  }
  if (!endInput.value) {
    endInput.value = getLocalDateString(new Date());
  }
}

// Hızlı Tarih Butonları
document.getElementById('report-quick-today')?.addEventListener('click', () => {
  const today = getLocalDateString();
  document.getElementById('report-start-date').value = today;
  document.getElementById('report-end-date').value = today;
  generateReport();
});

document.getElementById('report-quick-week')?.addEventListener('click', () => {
  const now = new Date();
  const day = now.getDay() || 7; // 1 = Pazartesi
  now.setDate(now.getDate() - (day - 1));
  document.getElementById('report-start-date').value = getLocalDateString(now);
  document.getElementById('report-end-date').value = getLocalDateString(new Date());
  generateReport();
});

document.getElementById('report-quick-month')?.addEventListener('click', () => {
  const d = new Date();
  d.setDate(1);
  document.getElementById('report-start-date').value = getLocalDateString(d);
  document.getElementById('report-end-date').value = getLocalDateString(new Date());
  generateReport();
});

document.getElementById('generate-report-btn')?.addEventListener('click', generateReport);
document.getElementById('print-report-btn')?.addEventListener('click', () => {
  window.print();
});

function generateReport() {
  const startDateStr = document.getElementById('report-start-date')?.value || '';
  const endDateStr = document.getElementById('report-end-date')?.value || '';

  // Yazdırma dönemi metinlerini doldur
  const printPeriod = document.getElementById('print-report-period');
  const printDate = document.getElementById('print-report-date');
  if (printPeriod) printPeriod.textContent = `Dönem: ${formatDateOnly(startDateStr)} - ${formatDateOnly(endDateStr)}`;
  if (printDate) printDate.textContent = `Rapor Alınma Zamanı: ${formatDateTime(new Date())}`;

  // Belirtilen tarih aralığındaki olayları filtrele
  const filteredEvents = state.events.filter(evt => {
    const evtDate = evt.date || (evt.timestamp ? getLocalDateString(new Date(evt.timestamp * 1000)) : '');
    if (startDateStr && evtDate < startDateStr) return false;
    if (endDateStr && evtDate > endDateStr) return false;
    return true;
  });

  const shiftStartMin = (state.settings.shiftStartHour * 60) + state.settings.shiftStartMinute;
  const shiftEndMin = (state.settings.shiftEndHour * 60) + state.settings.shiftEndMinute;

  // Personel bazında gruplama yapısı:
  // employeeMap[uid] = { name, days: { dateStr: { firstGiris, lastCikis, breaksCount } } }
  const employeeMap = {};

  // Önce aktif veya bilinen personelleri haritaya ekle
  state.employees.forEach(emp => {
    employeeMap[emp.uid] = {
      name: emp.name,
      uid: emp.uid,
      days: {}
    };
  });

  // Olayları işle
  filteredEvents.forEach(evt => {
    const uid = evt.uid;
    if (!uid) return;

    if (!employeeMap[uid]) {
      employeeMap[uid] = {
        name: evt.employeeName || uid,
        uid: uid,
        days: {}
      };
    }

    const evtDate = evt.date || (evt.timestamp ? getLocalDateString(new Date(evt.timestamp * 1000)) : '');
    if (!evtDate) return;

    if (!employeeMap[uid].days[evtDate]) {
      employeeMap[uid].days[evtDate] = {
        firstGiris: null,
        lastCikis: null,
        breaksCount: 0
      };
    }

    const dayObj = employeeMap[uid].days[evtDate];
    const ts = evt.timestamp;

    if (evt.type === 'GIRIS' && ts) {
      if (!dayObj.firstGiris || ts < dayObj.firstGiris) {
        dayObj.firstGiris = ts;
      }
    } else if ((evt.type === 'CIKIS' || evt.type === 'OTO_CIKIS') && ts) {
      if (!dayObj.lastCikis || ts > dayObj.lastCikis) {
        dayObj.lastCikis = ts;
      }
    } else if (evt.type === 'MOLA_BAS') {
      dayObj.breaksCount++;
    }
  });

  // Metrikleri hesapla
  let totalWorkDaysAll = 0;
  let totalLateAll = 0;
  let totalEarlyAll = 0;
  let totalBreaksAll = 0;

  const reportRows = [];

  Object.values(employeeMap).forEach(emp => {
    const dayKeys = Object.keys(emp.days);
    let workDaysCount = 0;
    let lateCount = 0;
    let earlyExitCount = 0;
    let breakCount = 0;

    let arrivalMinutesSum = 0;
    let arrivalCount = 0;

    let departureMinutesSum = 0;
    let departureCount = 0;

    dayKeys.forEach(dateKey => {
      const dayData = emp.days[dateKey];
      breakCount += dayData.breaksCount;

      // Eğer o gün en az 1 GIRIS varsa iş günü sayılır
      if (dayData.firstGiris) {
        workDaysCount++;
        const dArrival = new Date(dayData.firstGiris * 1000);
        const arrivalMin = (dArrival.getHours() * 60) + dArrival.getMinutes();
        arrivalMinutesSum += arrivalMin;
        arrivalCount++;

        // Geç kalma kontrolü
        if (arrivalMin > shiftStartMin) {
          lateCount++;
        }
      }

      // Erken çıkış kontrolü
      if (dayData.lastCikis) {
        const dDep = new Date(dayData.lastCikis * 1000);
        const depMin = (dDep.getHours() * 60) + dDep.getMinutes();
        departureMinutesSum += depMin;
        departureCount++;

        if (depMin < shiftEndMin) {
          earlyExitCount++;
        }
      }
    });

    if (workDaysCount > 0 || breakCount > 0) {
      totalWorkDaysAll += workDaysCount;
      totalLateAll += lateCount;
      totalEarlyAll += earlyExitCount;
      totalBreaksAll += breakCount;

      const avgArrivalStr = arrivalCount > 0 
        ? minutesToTimeString(Math.round(arrivalMinutesSum / arrivalCount)) 
        : '--:--';

      const avgDepartureStr = departureCount > 0 
        ? minutesToTimeString(Math.round(departureMinutesSum / departureCount)) 
        : '--:--';

      reportRows.push({
        name: emp.name,
        uid: emp.uid,
        workDays: workDaysCount,
        late: lateCount,
        earlyExit: earlyExitCount,
        breaks: breakCount,
        avgArrival: avgArrivalStr,
        avgDeparture: avgDepartureStr
      });
    }
  });

  // Özet sayaçları doldur
  document.getElementById('report-summary-days').textContent = totalWorkDaysAll;
  document.getElementById('report-summary-late').textContent = totalLateAll;
  document.getElementById('report-summary-early').textContent = totalEarlyAll;
  document.getElementById('report-summary-breaks').textContent = totalBreaksAll;

  // Tabloyu doldur
  const tableBody = document.getElementById('reports-table-body');
  const emptyState = document.getElementById('reports-empty-state');

  if (reportRows.length === 0) {
    tableBody.innerHTML = '';
    emptyState.classList.remove('hidden');
    return;
  }

  emptyState.classList.add('hidden');

  // İsme göre sırala
  reportRows.sort((a, b) => a.name.localeCompare(b.name, 'tr-TR'));

  tableBody.innerHTML = reportRows.map(row => {
    return `
      <tr class="hover:bg-slate-50 transition">
        <td class="py-3.5 px-4 font-semibold text-slate-900">
          <div>${escapeHtml(row.name)}</div>
          <span class="text-[11px] font-mono text-slate-400">${escapeHtml(row.uid)}</span>
        </td>
        <td class="py-3.5 px-4 text-center font-bold text-blue-700">${row.workDays} Gün</td>
        <td class="py-3.5 px-4 text-center">
          <span class="inline-flex items-center px-2 py-0.5 rounded-full text-xs font-semibold ${row.late > 0 ? 'bg-rose-50 text-rose-700 border border-rose-200' : 'text-slate-400'}">
            ${row.late} Kez
          </span>
        </td>
        <td class="py-3.5 px-4 text-center">
          <span class="inline-flex items-center px-2 py-0.5 rounded-full text-xs font-semibold ${row.earlyExit > 0 ? 'bg-amber-50 text-amber-700 border border-amber-200' : 'text-slate-400'}">
            ${row.earlyExit} Kez
          </span>
        </td>
        <td class="py-3.5 px-4 text-center font-medium text-slate-700">${row.breaks} Mola</td>
        <td class="py-3.5 px-4 text-center font-mono text-xs font-semibold text-slate-800">${row.avgArrival}</td>
        <td class="py-3.5 px-4 text-center font-mono text-xs font-semibold text-slate-800">${row.avgDeparture}</td>
      </tr>
    `;
  }).join('');
}

function minutesToTimeString(minutes) {
  const h = Math.floor(minutes / 60);
  const m = minutes % 60;
  return `${String(h).padStart(2, '0')}:${String(m).padStart(2, '0')}`;
}

// =============================================================================
// 11. TAB 5: AYARLAR (SETTINGS)
// =============================================================================

function populateSettingsForm() {
  const startHourStr = String(state.settings.shiftStartHour).padStart(2, '0');
  const startMinStr = String(state.settings.shiftStartMinute).padStart(2, '0');
  const endHourStr = String(state.settings.shiftEndHour).padStart(2, '0');
  const endMinStr = String(state.settings.shiftEndMinute).padStart(2, '0');

  const startInput = document.getElementById('settings-shift-start');
  const endInput = document.getElementById('settings-shift-end');
  const adminUidInput = document.getElementById('settings-admin-uid');
  const deviceIdInput = document.getElementById('settings-device-id');

  if (startInput) startInput.value = `${startHourStr}:${startMinStr}`;
  if (endInput) endInput.value = `${endHourStr}:${endMinStr}`;
  if (adminUidInput) adminUidInput.value = state.settings.adminUID || '00000000';
  if (deviceIdInput) deviceIdInput.value = state.settings.deviceId || 'ESP32_001';
}

// Mesai Saatlerini Kaydet
const settingsShiftForm = document.getElementById('settings-shift-form');
if (settingsShiftForm) {
  settingsShiftForm.addEventListener('submit', async (e) => {
    e.preventDefault();
    const startVal = document.getElementById('settings-shift-start').value;
    const endVal = document.getElementById('settings-shift-end').value;
    const spinner = document.getElementById('save-shift-spinner');
    const submitBtn = document.getElementById('save-shift-settings-btn');

    if (!startVal || !endVal) {
      showToast('Lütfen başlangıç ve bitiş saatlerini girin.', 'warning');
      return;
    }

    const [startH, startM] = startVal.split(':').map(Number);
    const [endH, endM] = endVal.split(':').map(Number);

    if (!db) return;

    submitBtn.disabled = true;
    spinner.classList.remove('hidden');

    try {
      await db.collection('settings').doc('main').set({
        shiftStartHour: startH,
        shiftStartMinute: startM,
        shiftEndHour: endH,
        shiftEndMinute: endM,
        updatedAt: firebase.firestore.FieldValue.serverTimestamp()
      }, { merge: true });

      showToast('Mesai saatleri başarıyla kaydedildi. Terminal 10 dakikada bir güncellenecektir.', 'success');
    } catch (err) {
      console.error('[Mesai Ayar Hatası]', err);
      showToast('Ayar kaydedilirken hata oluştu: ' + err.message, 'error');
    } finally {
      submitBtn.disabled = false;
      spinner.classList.add('hidden');
    }
  });
}

// Cihaz Bilgilerini Kaydet
const settingsDeviceForm = document.getElementById('settings-device-form');
if (settingsDeviceForm) {
  settingsDeviceForm.addEventListener('submit', async (e) => {
    e.preventDefault();
    const adminUid = document.getElementById('settings-admin-uid').value.trim();
    const spinner = document.getElementById('save-device-spinner');
    const submitBtn = document.getElementById('save-device-settings-btn');

    if (!db) return;

    submitBtn.disabled = true;
    spinner.classList.remove('hidden');

    try {
      await db.collection('settings').doc('main').set({
        adminUID: adminUid,
        updatedAt: firebase.firestore.FieldValue.serverTimestamp()
      }, { merge: true });

      showToast('Cihaz ve Admin kart bilgileri başarıyla kaydedildi.', 'success');
    } catch (err) {
      console.error('[Cihaz Ayar Hatası]', err);
      showToast('Cihaz ayarları kaydedilemedi: ' + err.message, 'error');
    } finally {
      submitBtn.disabled = false;
      spinner.classList.add('hidden');
    }
  });
}

// =============================================================================
// 12. BAŞLANGIÇ ÇALIŞTIRICISI (INIT)
// =============================================================================

document.addEventListener('DOMContentLoaded', () => {
  setupAuthListener();
});
